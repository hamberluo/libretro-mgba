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
	bool fading;         // handing back with main's tempo settled
	unsigned fadeFrames; // hand-back progress: rises while fading, falls back when active

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

static uint8_t romRead8(void* ctx, uint32_t a) {
	const struct RealtimeBgm* b = ctx;
	return a >= ROM_BASE && a - ROM_BASE < b->romSize ? b->rom[a - ROM_BASE] : 0;
}

// RAM headers are cries, and their address is reused: never music, never cached.
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

// Sticky, so a music player's intros and fanfares stay music.
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
	// mGBA refuses a state saved under a real BIOS while the PC is inside it
	// (a game waiting for VBlank) unless the loading core runs the same BIOS.
	if (b->mainGba->memory.fullBios) {
		s->core->loadBIOS(s->core, VFileFromConstMemory(b->mainGba->memory.bios, GBA_SIZE_BIOS), 0);
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

// Parked: only our calls may wake it.
static bool shadowClone(struct RealtimeBgm* b, struct Shadow* s) {
	size_t size = b->main->stateSize(b->main);
	void* state = malloc(size);
	b->main->saveState(b->main, state);
	bool loaded = s->core->loadState(s->core, state);
	free(state);
	if (!loaded) {
		return false;
	}
	struct ARMCore* cpu = s->gba->cpu;
	// Free below SP by the ABI, and game code never runs here again.
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	s->sp = cpu->gprs[ARM_SP];
	s->gba->memory.io[GBA_REG(IE)] = 0;
	s->gba->memory.io[GBA_REG(IME)] = 0;
	GBAHalt(s->gba);
	s->gba->audio.masterVolume = b->mainGba->audio.masterVolume;
	mAudioBufferClear(s->core->getAudioBuffer(s->core));
	return true;
}

// False if the call ran past the budget.
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

// Cries are built in RAM (species pitch, length); the clone's copy is stale.
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

static void copyControls(struct Shadow* s, struct GBA* main, uint32_t info) {
	// A moved tempo is speed compensation (Mercury): drop its pitch with it.
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

// Declarative: match main's state instead of replaying control calls.
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
	// Music ignores volume and pitch on purpose (cry ducking, compensation).
	if (s->sfx && mPlaying && bus32(s->gba, info + MP_HEADER) == mHeader) {
		copyControls(s, main, info);
	}
	return ok;
}

// Driver calls first: a core stops at VBlank with the game's VSync pending, and
// skipping it after a clone runs the PCM DMA past its buffer (FE8).
static bool shadowFrame(struct RealtimeBgm* b, struct Shadow* s) {
	for (int p = 0; p < b->playerCount; ++p) {
		if (!syncPlayer(b, s, b->players[p])) {
			return false;
		}
	}
	if (!shadowCall(s, b->m4a.soundVSync, 0, 0) || !shadowCall(s, b->m4a.soundMain, 0, 0)) {
		return false;
	}
	s->core->runFrame(s->core);
	return true;
}

static void fallBack(struct RealtimeBgm* b, enum retro_bgm_status why) {
	b->status = why;
	b->state = FELL_BACK;
	shadowDestroy(&b->bgm);
	shadowDestroy(&b->sfx);
	mAudioBufferClear(&b->bgmOut);
}

// False to retry on the next press: before m4aSoundInit there is no chain.
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

// A clone taken mid driver call keeps its ID+1 locks forever: release them as
// the interrupted call would.
static void releaseDriverLocks(struct RealtimeBgm* b, struct Shadow* s) {
	struct ARMCore* cpu = s->gba->cpu;
	uint32_t si = bus32(s->gba, SOUND_INFO_PTR);
	if (bus32(s->gba, si) == M4A_ID + 1) {
		cpu->memory.store32(cpu, si, M4A_ID, NULL);
	}
	for (int p = 0; p < b->playerCount; ++p) {
		if (bus32(s->gba, b->players[p] + MP_IDENT) == M4A_ID + 1) {
			cpu->memory.store32(cpu, b->players[p] + MP_IDENT, M4A_ID, NULL);
		}
	}
}

// False after falling back: a shadow that kept its own state plays nothing.
static bool cloneShadows(struct RealtimeBgm* b) {
	if (!shadowClone(b, &b->bgm) || !shadowClone(b, &b->sfx)) {
		fallBack(b, RETRO_BGM_CLONE_FAILED);
		return false;
	}
	releaseDriverLocks(b, &b->bgm);
	releaseDriverLocks(b, &b->sfx);
	mAudioBufferClear(&b->bgmOut);
	++clones;
	return true;
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
	// Untouched: each shadow's first sync stops what is not its own.
	if (cloneShadows(b)) {
		b->state = ACTIVE;
	}
}

static void finishHandBack(struct RealtimeBgm* b) {
	b->state = IDLE;
	b->fading = false;
	b->fadeFrames = 0;
	mAudioBufferClear(&b->bgmOut);
	mAudioBufferClear(&b->staging);
}

// The game has stopped compensating, on music and effects alike.
static bool tempoSettled(struct RealtimeBgm* b) {
	for (int p = 0; p < b->playerCount; ++p) {
		uint32_t info = b->players[p];
		if (!isPlaying(bus32(b->mainGba, info + MP_STATUS))) {
			continue;
		}
		const struct Shadow* owner = isLearned(b, info) || songLoops(b, bus32(b->mainGba, info + MP_HEADER)) ? &b->bgm : &b->sfx;
		if (bus16(b->mainGba, info + MP_TEMPO_U) != bus16(owner->gba, info + MP_TEMPO_U)) {
			return false;
		}
	}
	return true;
}

static double fadeAngle(const struct RealtimeBgm* b) {
	return M_PI / 2 * b->fadeFrames / CROSSFADE_FRAMES;
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
		if (b->state == ACTIVE) {
			// Pressed again mid fade: ramp back rather than snap (a click).
			if (b->fadeFrames > 0) {
				--b->fadeFrames;
			}
		} else if (!b->fading) {
			b->fading = tempoSettled(b) || ++b->waitFrames >= HANDBACK_WAIT_FRAMES;
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
	if (b->fadeFrames > 0) {
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
	case FELL_BACK:
		// Dart skips the forced mute on the load-time status: before sound init,
		// or after a fall back, compressed audio is still noise.
		return compressed ? silence(b, mainBuffer) : mainBuffer;
	case ACTIVE:
		if (compressed) {
			return silence(b, mainBuffer);
		}
		// fall through: a fade may still be ramping back
	case HANDING_BACK:
		if (b->fadeFrames > 0) {
			// Main sets the length: a leftover would pile up for the whole fade.
			struct mAudioBuffer* fx = b->sfx.core->getAudioBuffer(b->sfx.core);
			size_t n = mAudioBufferAvailable(mainBuffer);
			if (n > LANE_FRAMES) {
				n = LANE_FRAMES;
			}
			mAudioBufferRead(mainBuffer, b->mixA, n);
			mAudioBufferClear(mainBuffer);
			size_t got = mAudioBufferRead(fx, b->mixB, n);
			memset(b->mixB + got * 2, 0, (n - got) * 4);
			// Equal power against the uncorrelated music lane; the effects in main
			// and in the shadow are in phase, so theirs sum to one.
			double mainGain = sin(fadeAngle(b)), shadowGain = 1 - mainGain;
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
	if (!RealtimeBgmActive(b)) {
		return;
	}
	// A reset lands before m4aSoundInit: no driver would ever start on a clone.
	if (readChain(b)) {
		cloneShadows(b);
	} else if (b->state != FELL_BACK) {
		finishHandBack(b);
	}
}

unsigned RealtimeBgmClonesForTesting(void) {
	return clones;
}

struct mCore* RealtimeBgmShadowForTesting(struct RealtimeBgm* b, bool sfx) {
	return sfx ? b->sfx.core : b->bgm.core;
}

unsigned RealtimeBgmLiveShadowsForTesting(void) {
	return liveShadows;
}
