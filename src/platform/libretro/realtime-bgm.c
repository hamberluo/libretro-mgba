/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "realtime-bgm.h"

#include "aux-core.h"

#include <mgba/core/core.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SOUND_INFO_PTR 0x03007FF0u
#define M4A_ID 0x68736D53u
#define ROM_BASE 0x08000000u
// Return address for injected calls: the ROM entry, never reached by m4a code.
#define SENTINEL 0x08000001u
#define CALL_BUDGET 2000000
#define MAX_PLAYERS 64
#define LOOP_CACHE 1024
#define HANDBACK_WAIT_FRAMES 180 // 3 s
#define CROSSFADE_FRAMES 12      // 200 ms
#define LANE_FRAMES 0x1000

// MusicPlayerInfo
#define MP_HEADER 0x00
#define MP_STATUS 0x04
#define MP_TRACK_COUNT 0x08
#define MP_TEMPO_U 0x1E
#define MP_FADE_OI 0x24
#define MP_FADE_OC 0x26
#define MP_FADE_OV 0x28
#define MP_TRACKS 0x2C
#define MP_IDENT 0x34
#define MP_NEXT 0x3C
#define MP_STATUS_TRACKS 0xFFFFu
#define MP_STATUS_PAUSE 0x80000000u
#define TRACK_SIZE 0x50
#define SOUND_INFO_PLAYERS 0x24

enum State { IDLE, ACTIVE, HANDING_BACK, FELL_BACK };

struct Shadow {
	struct mCore* core;
	struct GBA* gba;
	mColor* video;
	uint32_t sp;
	bool sfx;
};

struct LoopEntry {
	uint32_t header;
	bool loops;
};

struct RealtimeBgm {
	struct mCore* main;
	struct GBA* mainGba;
	const uint8_t* rom;
	size_t romSize;
	struct RealtimeBgmM4a m4a;

	struct Shadow bgm;
	struct Shadow sfx;
	uint32_t players[MAX_PLAYERS];
	int playerCount;
	uint32_t learned[MAX_PLAYERS];
	int learnedCount;
	struct LoopEntry loops[LOOP_CACHE];

	enum State state;
	enum retro_bgm_status status;
	double speed;
	bool enabled;
	unsigned waitFrames;
	bool fading;
	unsigned fadeFrames;

	struct mAudioResampler bgmResampler;
	struct mAudioBuffer bgmOut;
	struct mAudioBuffer staging;
	int16_t mixA[LANE_FRAMES * 2];
	int16_t mixB[LANE_FRAMES * 2];
};

static unsigned liveShadows;
static unsigned clones;

static uint32_t bus32(struct GBA* gba, uint32_t a) {
	return gba->cpu->memory.load32(gba->cpu, a, NULL);
}

static uint16_t bus16(struct GBA* gba, uint32_t a) {
	return gba->cpu->memory.load16(gba->cpu, a, NULL);
}

static uint8_t bus8(struct GBA* gba, uint32_t a) {
	return gba->cpu->memory.load8(gba->cpu, a, NULL);
}

static bool isPlaying(uint32_t status) {
	return (status & MP_STATUS_TRACKS) && !(status & MP_STATUS_PAUSE);
}

static bool inRam(uint32_t a) {
	return (a >= 0x02000000 && a < 0x02040000) || (a >= 0x03000000 && a < 0x03008000);
}

static bool validIdent(uint32_t v) {
	return v - M4A_ID <= 1; // ID, or ID+1 while the driver holds its lock
}

// --- songs -------------------------------------------------------------------

static uint8_t romRead8(void* ctx, uint32_t a) {
	const struct RealtimeBgm* b = ctx;
	return a >= ROM_BASE && a - ROM_BASE < b->romSize ? b->rom[a - ROM_BASE] : 0;
}

// Only ROM songs are judged: a header the game builds in RAM is a cry, and
// its address is reused for other content, so it could not be cached anyway.
static bool songLoops(struct RealtimeBgm* b, uint32_t header) {
	if (header < ROM_BASE || header - ROM_BASE >= b->romSize) {
		return false;
	}
	unsigned i = (header >> 2) % LOOP_CACHE;
	for (unsigned n = 0; n < LOOP_CACHE; ++n, i = (i + 1) % LOOP_CACHE) {
		if (b->loops[i].header == header) {
			return b->loops[i].loops;
		}
		if (!b->loops[i].header) {
			b->loops[i].header = header;
			b->loops[i].loops = RealtimeBgmSongLoops(romRead8, b, header);
			return b->loops[i].loops;
		}
	}
	return RealtimeBgmSongLoops(romRead8, b, header);
}

static bool isLearned(const struct RealtimeBgm* b, uint32_t info) {
	for (int i = 0; i < b->learnedCount; ++i) {
		if (b->learned[i] == info) {
			return true;
		}
	}
	return false;
}

// A player that has played a looping song is a music player from then on, so
// its intros and fanfares stay with the music too.
static void learn(struct RealtimeBgm* b) {
	struct GBA* g = b->mainGba;
	uint32_t si = bus32(g, SOUND_INFO_PTR);
	if (!inRam(si) || !validIdent(bus32(g, si))) {
		return;
	}
	uint32_t pi = bus32(g, si + SOUND_INFO_PLAYERS);
	for (int n = 0; n < MAX_PLAYERS && inRam(pi) && validIdent(bus32(g, pi + MP_IDENT)); ++n, pi = bus32(g, pi + MP_NEXT)) {
		if (b->learnedCount < MAX_PLAYERS && !isLearned(b, pi) && isPlaying(bus32(g, pi + MP_STATUS)) &&
		    songLoops(b, bus32(g, pi + MP_HEADER))) {
			b->learned[b->learnedCount++] = pi;
		}
	}
}

// --- shadow cores --------------------------------------------------------------

static bool shadowCreate(struct RealtimeBgm* b, struct Shadow* s) {
	s->core = mCoreCreate(mPLATFORM_GBA);
	if (!s->core) {
		return false;
	}
	mCoreInitConfig(s->core, NULL);
	s->core->init(s->core);
	RetroAuxCoreConfigure(s->core);
	struct VFile* rom = VFileFromConstMemory(b->rom, b->romSize);
	if (!rom || !s->core->loadROM(s->core, rom)) {
		RetroAuxCoreDestroy(s->core);
		s->core = NULL;
		return false;
	}
	// Before reset: that is when a core picks its renderer.
	s->video = calloc(256 * 224, sizeof(mColor));
	s->core->setVideoBuffer(s->core, s->video, 256);
	s->core->reset(s->core);
	RetroAuxCoreNeverDraw(s->core);
	s->gba = s->core->board;
	++liveShadows;
	return true;
}

static void shadowDestroy(struct Shadow* s) {
	if (s->core) {
		RetroAuxCoreDestroy(s->core);
		--liveShadows;
	}
	free(s->video);
	s->core = NULL;
	s->gba = NULL;
	s->video = NULL;
}

// Main's state, then parked: nothing but our calls may wake the CPU.
static void shadowClone(struct RealtimeBgm* b, struct Shadow* s) {
	size_t size = b->main->stateSize(b->main);
	void* state = malloc(size);
	b->main->saveState(b->main, state);
	s->core->loadState(s->core, state);
	free(state);
	struct ARMCore* cpu = s->gba->cpu;
	// Below the system-mode SP is free stack by the ABI, and game code never
	// runs on a shadow again.
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	s->sp = cpu->gprs[ARM_SP];
	s->gba->memory.io[GBA_REG(IE)] = 0;
	s->gba->memory.io[GBA_REG(IME)] = 0;
	GBAHalt(s->gba);
	s->gba->audio.masterVolume = b->mainGba->audio.masterVolume;
	mAudioBufferClear(s->core->getAudioBuffer(s->core));
}

// Calls a Thumb function on the parked CPU; false if it ran past the budget.
static bool shadowCall(struct Shadow* s, uint32_t fn, uint32_t r0, uint32_t r1) {
	struct ARMCore* cpu = s->gba->cpu;
	cpu->halted = 0;
	cpu->cpsr.i = 1;
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	cpu->gprs[0] = r0;
	cpu->gprs[1] = r1;
	cpu->gprs[ARM_SP] = s->sp;
	cpu->gprs[ARM_LR] = SENTINEL;
	cpu->cpsr.t = 1;
	_ARMSetMode(cpu, MODE_THUMB);
	cpu->gprs[ARM_PC] = fn & ~1u;
	ThumbWritePC(cpu);
	for (long budget = CALL_BUDGET; budget > 0; --budget) {
		uint32_t pc = cpu->gprs[ARM_PC];
		if (pc >= (SENTINEL & ~1u) && pc <= (SENTINEL & ~1u) + 8) { // pc runs ahead by the prefetch
			GBAHalt(s->gba);
			return true;
		}
		ARMRun(cpu);
	}
	GBAHalt(s->gba);
	return false;
}

static void copyBytes(struct Shadow* s, struct GBA* main, uint32_t a, uint32_t n) {
	for (uint32_t i = 0; i < n; ++i) {
		s->gba->cpu->memory.store8(s->gba->cpu, a + i, bus8(main, a + i), NULL);
	}
}

// A header the game built in RAM (Pokemon cries carry per-species pitch and
// length there): the shadow's copy dates from the clone, so bring it up to date.
static void copyRamSong(struct Shadow* s, struct GBA* main, uint32_t header) {
	if (!inRam(header)) {
		return;
	}
	unsigned n = bus8(main, header);
	if (n > 16) {
		n = 16;
	}
	copyBytes(s, main, header, 8 + 4 * n);
	for (unsigned t = 0; t < n; ++t) {
		uint32_t track = bus32(main, header + 8 + 4 * t);
		if (inRam(track)) {
			copyBytes(s, main, track, 0x100);
		}
	}
	uint32_t tone = bus32(main, header + 4);
	if (inRam(tone)) {
		copyBytes(s, main, tone, 12);
	}
}

// Per-track volume/pan/pitch the game applied after starting the song: what
// m4aMPlayVolumeControl / PanpotControl / PitchControl leave behind.
static void copyControls(struct Shadow* s, struct GBA* main, uint32_t info) {
	// A player whose tempo the game moved is being compensated for the speed
	// (Mercury); the shadow already ignores tempo, so that pitch goes too.
	bool pitch = bus16(main, info + MP_TEMPO_U) == 0x100;
	static const struct {
		uint8_t offset;
		uint8_t flag;
	} fields[] = { { 0x13, 0x03 }, { 0x15, 0x03 }, { 0x0B, 0x0C }, { 0x0D, 0x0C } }; // volX panX: VOLCHG; keyShiftX pitX: PITCHG
	struct ARMCore* sc = s->gba->cpu;
	uint32_t tracks = bus32(main, info + MP_TRACKS);
	unsigned n = bus8(main, info + MP_TRACK_COUNT);
	if (n > 16) {
		n = 16;
	}
	for (unsigned t = 0; t < n; ++t) {
		uint32_t track = tracks + t * TRACK_SIZE;
		uint8_t flags = 0;
		for (unsigned i = 0; i < (pitch ? 4u : 2u); ++i) {
			uint8_t v = bus8(main, track + fields[i].offset);
			if (v != bus8(s->gba, track + fields[i].offset)) {
				sc->memory.store8(sc, track + fields[i].offset, v, NULL);
				flags |= fields[i].flag;
			}
		}
		if (flags) {
			sc->memory.store8(sc, track, bus8(s->gba, track) | flags, NULL);
		}
	}
}

// Declarative: make the shadow's player match main's for the songs this
// shadow owns, instead of replaying the game's control calls.
static bool syncPlayer(struct RealtimeBgm* b, struct Shadow* s, uint32_t info) {
	struct GBA* main = b->mainGba;
	struct ARMCore* sc = s->gba->cpu;
	uint32_t mHeader = bus32(main, info + MP_HEADER), sHeader = bus32(s->gba, info + MP_HEADER);
	uint32_t mStatus = bus32(main, info + MP_STATUS), sStatus = bus32(s->gba, info + MP_STATUS);
	bool mPlaying = isPlaying(mStatus), sPlaying = isPlaying(sStatus);
	bool music = isLearned(b, info) || songLoops(b, mHeader);
	bool wanted = s->sfx ? !music : music;
	bool ok = true;
	if (mPlaying && wanted && (mHeader != sHeader || !(sStatus & MP_STATUS_TRACKS))) {
		copyRamSong(s, main, mHeader);
		ok = shadowCall(s, b->m4a.mplayStart, info, mHeader);
	} else if (mPlaying && wanted && !sPlaying) {
		// Same song resumed: what m4aMPlayContinue does.
		sc->memory.store32(sc, info + MP_STATUS, sStatus & ~MP_STATUS_PAUSE, NULL);
	} else if (sPlaying && (!mPlaying || !wanted || mHeader != sHeader)) {
		ok = shadowCall(s, b->m4a.mplayStop, info, 0);
	}
	// A fade the game began: what m4aMPlayFadeOut/In leave behind.
	uint16_t mFade = bus16(main, info + MP_FADE_OI);
	if (mFade && !bus16(s->gba, info + MP_FADE_OI)) {
		sc->memory.store16(sc, info + MP_FADE_OI, mFade, NULL);
		sc->memory.store16(sc, info + MP_FADE_OC, bus16(main, info + MP_FADE_OC), NULL);
		sc->memory.store16(sc, info + MP_FADE_OV, bus16(main, info + MP_FADE_OV), NULL);
	}
	// Music ignores volume and pitch control on purpose (cry ducking, speed
	// compensation); sound effects need them.
	if (s->sfx && mPlaying && bus32(s->gba, info + MP_HEADER) == mHeader) {
		copyControls(s, main, info);
	}
	return ok;
}

// One frame: sync from main, run to VBlank, then the driver's per-frame calls.
static bool shadowFrame(struct RealtimeBgm* b, struct Shadow* s) {
	for (int p = 0; p < b->playerCount; ++p) {
		if (!syncPlayer(b, s, b->players[p])) {
			return false;
		}
	}
	s->core->runFrame(s->core);
	return shadowCall(s, b->m4a.soundVSync, 0, 0) && shadowCall(s, b->m4a.soundMain, 0, 0);
}

// --- state machine -------------------------------------------------------------

static void fallBack(struct RealtimeBgm* b, enum retro_bgm_status why) {
	b->status = why;
	b->state = FELL_BACK;
	shadowDestroy(&b->bgm);
	shadowDestroy(&b->sfx);
	mAudioBufferClear(&b->bgmOut);
}

// The driver's own player chain. False to stay out for now: m4aSoundInit has
// not run (boot logo) or opened no players yet.
static bool readChain(struct RealtimeBgm* b) {
	struct GBA* g = b->mainGba;
	uint32_t si = bus32(g, SOUND_INFO_PTR);
	if (!inRam(si) || !validIdent(bus32(g, si))) {
		return false;
	}
	b->playerCount = 0;
	for (uint32_t pi = bus32(g, si + SOUND_INFO_PLAYERS); inRam(pi) && validIdent(bus32(g, pi + MP_IDENT));
	     pi = bus32(g, pi + MP_NEXT)) {
		for (int q = 0; q < b->playerCount; ++q) {
			if (b->players[q] == pi) {
				fallBack(b, RETRO_BGM_CHAIN_INVALID);
				return false;
			}
		}
		if (b->playerCount == MAX_PLAYERS) {
			fallBack(b, RETRO_BGM_CHAIN_INVALID);
			return false;
		}
		b->players[b->playerCount++] = pi;
	}
	return b->playerCount > 0;
}

static void cloneShadows(struct RealtimeBgm* b) {
	shadowClone(b, &b->bgm);
	shadowClone(b, &b->sfx);
	mAudioBufferClear(&b->bgmOut);
	++clones;
}

static void takeOver(struct RealtimeBgm* b) {
	if (!readChain(b)) {
		return;
	}
	if (!b->bgm.core && !shadowCreate(b, &b->bgm)) {
		return;
	}
	if (!b->sfx.core && !shadowCreate(b, &b->sfx)) {
		return;
	}
	b->sfx.sfx = true;
	// Untouched at the clone: what is sounding keeps sounding, and each
	// shadow's first sync stops what is not its own.
	cloneShadows(b);
	b->state = ACTIVE;
}

static void finishHandBack(struct RealtimeBgm* b) {
	b->state = IDLE;
	b->fading = false;
	mAudioBufferClear(&b->bgmOut);
	mAudioBufferClear(&b->staging);
}

// Every playing music player runs at the same tempo in main as in the music
// shadow: the game is no longer compensating for a speed it measured.
static bool tempoSettled(struct RealtimeBgm* b) {
	for (int p = 0; p < b->playerCount; ++p) {
		uint32_t info = b->players[p];
		if (isLearned(b, info) && isPlaying(bus32(b->mainGba, info + MP_STATUS)) &&
		    bus16(b->mainGba, info + MP_TEMPO_U) != bus16(b->bgm.gba, info + MP_TEMPO_U)) {
			return false;
		}
	}
	return true;
}

static double fadeAngle(const struct RealtimeBgm* b) {
	return M_PI / 2 * (b->fading ? (double) b->fadeFrames / CROSSFADE_FRAMES : 0);
}

struct RealtimeBgm* RealtimeBgmCreate(struct mCore* main, const void* rom, size_t romSize,
                                      const struct RealtimeBgmM4a* m4a, unsigned outRate) {
	struct RealtimeBgm* b = calloc(1, sizeof(*b));
	if (!b) {
		return NULL;
	}
	b->main = main;
	b->mainGba = main->board;
	b->rom = rom;
	b->romSize = romSize;
	b->m4a = *m4a;
	b->status = RETRO_BGM_OK;
	b->speed = 1;
	mAudioBufferInit(&b->bgmOut, LANE_FRAMES * 2, 2);
	mAudioBufferInit(&b->staging, LANE_FRAMES, 2);
	mAudioResamplerInit(&b->bgmResampler, mINTERPOLATOR_SINC);
	mAudioResamplerSetDestination(&b->bgmResampler, &b->bgmOut, outRate);
	return b;
}

void RealtimeBgmDestroy(struct RealtimeBgm* b) {
	if (!b) {
		return;
	}
	shadowDestroy(&b->bgm);
	shadowDestroy(&b->sfx);
	mAudioResamplerDeinit(&b->bgmResampler);
	mAudioBufferDeinit(&b->bgmOut);
	mAudioBufferDeinit(&b->staging);
	free(b);
}

void RealtimeBgmSetSpeed(struct RealtimeBgm* b, double speed, bool enabled) {
	b->speed = speed;
	b->enabled = enabled;
	bool wanted = speed > 1 && enabled;
	switch (b->state) {
	case IDLE:
		if (wanted) {
			takeOver(b);
		}
		break;
	case ACTIVE:
		if (!wanted) {
			b->state = HANDING_BACK;
			b->waitFrames = 0;
			b->fading = false;
			b->fadeFrames = 0;
		}
		break;
	case HANDING_BACK:
		// Recloning now would inherit any compensation main is still undoing.
		if (wanted) {
			b->state = ACTIVE;
			b->fading = false;
		}
		break;
	case FELL_BACK:
		break;
	}
}

void RealtimeBgmAfterMainFrame(struct RealtimeBgm* b) {
	learn(b);
	if (b->state != ACTIVE && b->state != HANDING_BACK) {
		return;
	}
	if (b->state == ACTIVE && b->speed > RETRO_BGM_SFX_SPEED_MAX) {
		return;
	}
	if (!shadowFrame(b, &b->sfx)) {
		fallBack(b, RETRO_BGM_BUDGET);
	}
}

void RealtimeBgmRun(struct RealtimeBgm* b, unsigned frames) {
	for (unsigned f = 0; f < frames && (b->state == ACTIVE || b->state == HANDING_BACK); ++f) {
		if (!shadowFrame(b, &b->bgm)) {
			fallBack(b, RETRO_BGM_BUDGET);
			return;
		}
		struct mAudioBuffer* out = b->bgm.core->getAudioBuffer(b->bgm.core);
		mAudioResamplerSetSource(&b->bgmResampler, out, b->bgm.core->audioSampleRate(b->bgm.core), true);
		mAudioResamplerProcess(&b->bgmResampler);
		if (b->state != HANDING_BACK) {
			continue;
		}
		if (!b->fading) {
			if (tempoSettled(b) || ++b->waitFrames >= HANDBACK_WAIT_FRAMES) {
				b->fading = true;
				b->fadeFrames = 0;
			}
		} else if (++b->fadeFrames >= CROSSFADE_FRAMES) {
			finishHandBack(b);
		}
	}
}

size_t RealtimeBgmRead(struct RealtimeBgm* b, int16_t* out, size_t maxFrames) {
	if (b->state != ACTIVE && b->state != HANDING_BACK) {
		mAudioBufferClear(&b->bgmOut);
		return 0;
	}
	size_t n = mAudioBufferRead(&b->bgmOut, out, maxFrames);
	if (b->fading) {
		double gain = cos(fadeAngle(b));
		for (size_t i = 0; i < n * 2; ++i) {
			out[i] = (int16_t) (out[i] * gain);
		}
	}
	return n;
}

static struct mAudioBuffer* silence(struct RealtimeBgm* b, struct mAudioBuffer* mainBuffer) {
	size_t n = mAudioBufferAvailable(mainBuffer);
	mAudioBufferClear(mainBuffer);
	if (n > LANE_FRAMES) {
		n = LANE_FRAMES;
	}
	memset(b->mixA, 0, n * 4);
	mAudioBufferWrite(&b->staging, b->mixA, n);
	return &b->staging;
}

struct mAudioBuffer* RealtimeBgmGameAudio(struct RealtimeBgm* b, struct mAudioBuffer* mainBuffer) {
	bool compressed = b->enabled && b->speed > RETRO_BGM_SFX_SPEED_MAX;
	switch (b->state) {
	case IDLE:
		return mainBuffer;
	case FELL_BACK:
		// Mute policy upstream only knew the status at load time.
		return compressed ? silence(b, mainBuffer) : mainBuffer;
	case ACTIVE:
		if (compressed) {
			return silence(b, mainBuffer);
		}
		break;
	case HANDING_BACK:
		if (b->fading) {
			struct mAudioBuffer* fx = b->sfx.core->getAudioBuffer(b->sfx.core);
			size_t n = mAudioBufferAvailable(mainBuffer);
			if (mAudioBufferAvailable(fx) < n) {
				n = mAudioBufferAvailable(fx);
			}
			if (n > LANE_FRAMES) {
				n = LANE_FRAMES;
			}
			mAudioBufferRead(mainBuffer, b->mixA, n);
			mAudioBufferRead(fx, b->mixB, n);
			double angle = fadeAngle(b), shadowGain = cos(angle), mainGain = sin(angle);
			for (size_t i = 0; i < n * 2; ++i) {
				b->mixA[i] = (int16_t) (b->mixA[i] * mainGain + b->mixB[i] * shadowGain);
			}
			mAudioBufferWrite(&b->staging, b->mixA, n);
			return &b->staging;
		}
		break;
	}
	mAudioBufferClear(mainBuffer);
	return b->sfx.core->getAudioBuffer(b->sfx.core);
}

bool RealtimeBgmActive(const struct RealtimeBgm* b) {
	return b->state == ACTIVE || b->state == HANDING_BACK;
}

enum retro_bgm_status RealtimeBgmStatus(const struct RealtimeBgm* b) {
	return b->status;
}

void RealtimeBgmMainJumped(struct RealtimeBgm* b) {
	if (RealtimeBgmActive(b)) {
		cloneShadows(b);
	}
}

unsigned RealtimeBgmClonesForTesting(void) {
	return clones;
}

unsigned RealtimeBgmLiveShadowsForTesting(void) {
	return liveShadows;
}
