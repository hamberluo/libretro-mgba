/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "realtime-bgm.h"
#include "realtime-bgm-engine.h"

#include <mgba/core/core.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba-util/audio-buffer.h>

#include <stdlib.h>

#define SOUND_INFO_PTR 0x03007FF0u
#define M4A_ID 0x68736D53u
#define ROM_BASE 0x08000000u
// Return address for injected calls: the ROM entry, never reached by m4a code.
#define SENTINEL 0x08000001u
#define CALL_BUDGET 2000000
#define MAX_PLAYERS 64
#define MUSIC_CACHE 1024

// MusicPlayerInfo
#define MP_HEADER 0x00
#define MP_STATUS 0x04
#define MP_TRACK_COUNT 0x08
#define MP_CLOCK 0x0C
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
#define SOUND_INFO_MAIN_HEAD 0x20
#define SOUND_INFO_PLAYERS 0x24

struct MusicEntry {
	uint32_t header;
	bool music;
};

struct M4a {
	struct mCore* main;
	struct GBA* mainGba;
	const uint8_t* rom;
	size_t romSize;
	struct RealtimeBgmM4a m4a;

	uint32_t players[MAX_PLAYERS];
	int playerCount;
	uint32_t learned[MAX_PLAYERS];
	int learnedCount;
	struct MusicEntry music[MUSIC_CACHE];

	// Per shadow, indexed by RealtimeBgmShadow.sfx.
	uint32_t sp[2];
	uint32_t mainClock[2][MAX_PLAYERS]; // per player, as of the last sync
};

static struct GBA* gbaOf(const struct RealtimeBgmShadow* s) {
	return s->core->board;
}

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
	const struct M4a* m = ctx;
	return a >= ROM_BASE && a - ROM_BASE < m->romSize ? m->rom[a - ROM_BASE] : 0;
}

// RAM headers are cries, and their address is reused: never music, never cached.
static bool musicSong(struct M4a* m, uint32_t header) {
	if (header < ROM_BASE || header - ROM_BASE >= m->romSize) {
		return false;
	}
	unsigned i = (header >> 2) % MUSIC_CACHE;
	for (unsigned n = 0; n < MUSIC_CACHE; ++n, i = (i + 1) % MUSIC_CACHE) {
		if (m->music[i].header == header) {
			return m->music[i].music;
		}
		if (!m->music[i].header) {
			m->music[i].header = header;
			m->music[i].music = RealtimeBgmSongIsMusic(romRead8, m, header);
			return m->music[i].music;
		}
	}
	return RealtimeBgmSongIsMusic(romRead8, m, header);
}

static bool isLearned(const struct M4a* m, uint32_t info) {
	for (int i = 0; i < m->learnedCount; ++i) {
		if (m->learned[i] == info) {
			return true;
		}
	}
	return false;
}

// Sticky, so a music player's intros and fanfares stay music.
static void learn(struct M4a* m) {
	struct GBA* g = m->mainGba;
	uint32_t si = bus32(g, SOUND_INFO_PTR);
	if (!inRam(si) || !validIdent(bus32(g, si))) {
		return;
	}
	uint32_t pi = bus32(g, si + SOUND_INFO_PLAYERS);
	for (int n = 0; n < MAX_PLAYERS && inRam(pi) && validIdent(bus32(g, pi + MP_IDENT)); ++n, pi = bus32(g, pi + MP_NEXT)) {
		if (m->learnedCount < MAX_PLAYERS && !isLearned(m, pi) && isPlaying(bus32(g, pi + MP_STATUS)) &&
		    musicSong(m, bus32(g, pi + MP_HEADER))) {
			m->learned[m->learnedCount++] = pi;
		}
	}
}

// Only MPlayStart resets a player's clock, and it shows a frame before the
// player plays again, so a stopped player reads as the latest possible time.
static uint32_t startClock(struct GBA* g, uint32_t info) {
	return bus32(g, info + MP_STATUS) & MP_STATUS_TRACKS ? bus32(g, info + MP_CLOCK) : UINT32_MAX;
}

// False if the call ran past the budget.
static bool shadowCall(struct M4a* m, struct RealtimeBgmShadow* s, uint32_t fn, uint32_t r0, uint32_t r1) {
	struct ARMCore* cpu = gbaOf(s)->cpu;
	cpu->halted = 0;
	cpu->cpsr.i = 1;
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	cpu->gprs[0] = r0;
	cpu->gprs[1] = r1;
	cpu->gprs[ARM_SP] = m->sp[s->sfx];
	cpu->gprs[ARM_LR] = SENTINEL;
	cpu->cpsr.t = 1;
	_ARMSetMode(cpu, MODE_THUMB);
	cpu->gprs[ARM_PC] = fn & ~1u;
	ThumbWritePC(cpu);
	for (long budget = CALL_BUDGET; budget > 0; --budget) {
		uint32_t pc = cpu->gprs[ARM_PC];
		if (pc >= (SENTINEL & ~1u) && pc <= (SENTINEL & ~1u) + 8) { // pc runs ahead by the prefetch
			GBAHalt(gbaOf(s));
			return true;
		}
		ARMRun(cpu);
	}
	GBAHalt(gbaOf(s));
	return false;
}

static void copyBytes(struct RealtimeBgmShadow* s, struct GBA* main, uint32_t a, uint32_t n) {
	for (uint32_t i = 0; i < n; ++i) {
		gbaOf(s)->cpu->memory.store8(gbaOf(s)->cpu, a + i, bus8(main, a + i), NULL);
	}
}

// Cries are built in RAM (species pitch, length); the clone's copy is stale.
static void copyRamSong(struct RealtimeBgmShadow* s, struct GBA* main, uint32_t header) {
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

static void copyControls(struct RealtimeBgmShadow* s, struct GBA* main, uint32_t info) {
	// A moved tempo is speed compensation (Mercury): drop its pitch with it.
	bool pitch = bus16(main, info + MP_TEMPO_U) == 0x100;
	static const struct {
		uint8_t offset;
		uint8_t flag;
	} fields[] = { { 0x13, 0x03 }, { 0x15, 0x03 }, { 0x0B, 0x0C }, { 0x0D, 0x0C } }; // volX panX: VOLCHG; keyShiftX pitX: PITCHG
	struct ARMCore* sc = gbaOf(s)->cpu;
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
			if (v != bus8(gbaOf(s), track + fields[i].offset)) {
				sc->memory.store8(sc, track + fields[i].offset, v, NULL);
				flags |= fields[i].flag;
			}
		}
		if (flags) {
			sc->memory.store8(sc, track, bus8(gbaOf(s), track) | flags, NULL);
		}
	}
}

// Declarative: match main's state instead of replaying control calls.
static bool syncPlayer(struct M4a* m, struct RealtimeBgmShadow* s, int p) {
	uint32_t info = m->players[p];
	struct GBA* main = m->mainGba;
	struct ARMCore* sc = gbaOf(s)->cpu;
	uint32_t mHeader = bus32(main, info + MP_HEADER), sHeader = bus32(gbaOf(s), info + MP_HEADER);
	uint32_t mStatus = bus32(main, info + MP_STATUS), sStatus = bus32(gbaOf(s), info + MP_STATUS);
	bool mPlaying = isPlaying(mStatus), sPlaying = isPlaying(sStatus);
	bool music = isLearned(m, info) || musicSong(m, mHeader);
	bool wanted = s->sfx ? !music : music;
	// The shadow finishing first is no restart: Mercury slows main's effects
	// for the speed it saw.
	uint32_t clock = startClock(main, info);
	bool restarted = clock < m->mainClock[s->sfx][p];
	m->mainClock[s->sfx][p] = clock;
	bool ok = true;
	if (mPlaying && wanted && (mHeader != sHeader || restarted)) {
		copyRamSong(s, main, mHeader);
		ok = shadowCall(m, s, m->m4a.mplayStart, info, mHeader);
	} else if (mPlaying && wanted && !sPlaying) {
		// Same song resumed: what m4aMPlayContinue does.
		sc->memory.store32(sc, info + MP_STATUS, sStatus & ~MP_STATUS_PAUSE, NULL);
	} else if (sPlaying && (!mPlaying || !wanted || mHeader != sHeader)) {
		ok = shadowCall(m, s, m->m4a.mplayStop, info, 0);
	}
	// A fade the game began: what m4aMPlayFadeOut/In leave behind.
	uint16_t mFade = bus16(main, info + MP_FADE_OI);
	if (mFade && !bus16(gbaOf(s), info + MP_FADE_OI)) {
		sc->memory.store16(sc, info + MP_FADE_OI, mFade, NULL);
		sc->memory.store16(sc, info + MP_FADE_OC, bus16(main, info + MP_FADE_OC), NULL);
		sc->memory.store16(sc, info + MP_FADE_OV, bus16(main, info + MP_FADE_OV), NULL);
	}
	// Music ignores volume and pitch on purpose (cry ducking, compensation).
	if (s->sfx && mPlaying && bus32(gbaOf(s), info + MP_HEADER) == mHeader) {
		copyControls(s, main, info);
	}
	return ok;
}

// Driver calls first: a core stops at VBlank with the game's VSync pending, and
// skipping it after a clone runs the PCM DMA past its buffer (FE8).
static bool frame(void* state, struct RealtimeBgmShadow* s) {
	struct M4a* m = state;
	for (int p = 0; p < m->playerCount; ++p) {
		if (!syncPlayer(m, s, p)) {
			return false;
		}
	}
	if (!shadowCall(m, s, m->m4a.soundVSync, 0, 0) || !shadowCall(m, s, m->m4a.soundMain, 0, 0)) {
		return false;
	}
	s->core->runFrame(s->core);
	return true;
}

// False to retry on the next press: before m4aSoundInit there is no chain.
static bool readChain(struct M4a* m, enum retro_bgm_status* fail) {
	struct GBA* g = m->mainGba;
	uint32_t si = bus32(g, SOUND_INFO_PTR);
	// The official BIOS plays its boot chime with its own copy of the driver,
	// ident included; only its function pointers give it away.
	if (!inRam(si) || !validIdent(bus32(g, si)) || bus32(g, si + SOUND_INFO_MAIN_HEAD) < GBA_SIZE_BIOS) {
		return false;
	}
	m->playerCount = 0;
	for (uint32_t pi = bus32(g, si + SOUND_INFO_PLAYERS); inRam(pi) && validIdent(bus32(g, pi + MP_IDENT));
	     pi = bus32(g, pi + MP_NEXT)) {
		for (int q = 0; q < m->playerCount; ++q) {
			if (m->players[q] == pi) {
				*fail = RETRO_BGM_CHAIN_INVALID;
				return false;
			}
		}
		if (m->playerCount == MAX_PLAYERS) {
			*fail = RETRO_BGM_CHAIN_INVALID;
			return false;
		}
		m->players[m->playerCount++] = pi;
	}
	return m->playerCount > 0;
}

// A clone taken mid driver call keeps its ID+1 locks forever: release them as
// the interrupted call would.
static void releaseDriverLocks(struct M4a* m, struct RealtimeBgmShadow* s) {
	struct ARMCore* cpu = gbaOf(s)->cpu;
	uint32_t si = bus32(gbaOf(s), SOUND_INFO_PTR);
	if (bus32(gbaOf(s), si) == M4A_ID + 1) {
		cpu->memory.store32(cpu, si, M4A_ID, NULL);
	}
	for (int p = 0; p < m->playerCount; ++p) {
		if (bus32(gbaOf(s), m->players[p] + MP_IDENT) == M4A_ID + 1) {
			cpu->memory.store32(cpu, m->players[p] + MP_IDENT, M4A_ID, NULL);
		}
	}
}

// The game has stopped compensating, on music and effects alike.
static bool tempoSettled(struct M4a* m, const struct RealtimeBgmShadow* bgm, const struct RealtimeBgmShadow* sfx) {
	for (int p = 0; p < m->playerCount; ++p) {
		uint32_t info = m->players[p];
		if (!isPlaying(bus32(m->mainGba, info + MP_STATUS))) {
			continue;
		}
		const struct RealtimeBgmShadow* owner = isLearned(m, info) || musicSong(m, bus32(m->mainGba, info + MP_HEADER)) ? bgm : sfx;
		if (bus16(m->mainGba, info + MP_TEMPO_U) != bus16(gbaOf(owner), info + MP_TEMPO_U)) {
			return false;
		}
	}
	return true;
}

static bool ready(void* state, enum retro_bgm_status* fail) {
	return readChain(state, fail);
}

// Parked: only our calls may wake it.
static void cloned(void* state, struct RealtimeBgmShadow* s) {
	struct M4a* m = state;
	struct GBA* g = gbaOf(s);
	struct ARMCore* cpu = g->cpu;
	// Free below SP by the ABI, and game code never runs here again.
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	m->sp[s->sfx] = cpu->gprs[ARM_SP];
	g->memory.io[GBA_REG(IE)] = 0;
	g->memory.io[GBA_REG(IME)] = 0;
	GBAHalt(g);
	g->audio.masterVolume = m->mainGba->audio.masterVolume;
	for (int p = 0; p < m->playerCount; ++p) {
		m->mainClock[s->sfx][p] = startClock(m->mainGba, m->players[p]);
	}
	releaseDriverLocks(m, s);
}

static bool settled(void* state, const struct RealtimeBgmShadow* bgm, const struct RealtimeBgmShadow* sfx) {
	return tempoSettled(state, bgm, sfx);
}

static void observe(void* state) {
	learn(state);
}

bool RealtimeBgmM4aEngine(struct mCore* main, const void* rom, size_t romSize, const struct RealtimeBgmM4a* m4a,
                          struct RealtimeBgmEngine* out) {
	struct M4a* m = calloc(1, sizeof(*m));
	if (!m) {
		return false;
	}
	m->main = main;
	m->mainGba = main->board;
	m->rom = rom;
	m->romSize = romSize;
	m->m4a = *m4a;
	*out = (struct RealtimeBgmEngine) {
		.state = m,
		.ready = ready,
		.cloned = cloned,
		.frame = frame,
		.settled = settled,
		.observe = observe,
		.destroy = free,
	};
	return true;
}
