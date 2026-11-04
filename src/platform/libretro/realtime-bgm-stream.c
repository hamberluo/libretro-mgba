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

#include <stdlib.h>
#include <string.h>

// A game's own streamed player that describes itself (Langrisser II GBA, gba/audio.c), 4-aligned in ROM:
//   char tag[8] "RTBGMS01"; u32 log, apply, frame; u16 musicLane, sfxLane
// log (RAM): u32 magic 'RTBG' once the player is up, u32 count, u8 id[32]: every command the game played.
// apply(id) plays one command, frame() is the player's VBlank work (Thumb entries). The lanes are the
// SOUNDCNT_H enable bits each lane plays on; a shadow keeps its own lane and mutes the other.

#define ROM_BASE 0x08000000u
#define TAG "RTBGMS01"
#define LOG_MAGIC 0x52544247u
#define LOG_N 32
#define SENTINEL 0x08000001u // return address for our calls: the ROM entry, never reached by the player
#define CALL_BUDGET 2000000

struct Stream {
	struct mCore* main;
	struct GBA* mainGba;
	struct RealtimeBgmStream d;
	uint32_t sp[2];
	uint32_t seen[2]; // per shadow (RealtimeBgmShadow.sfx): main's log count already applied
};

static struct GBA* gbaOf(const struct RealtimeBgmShadow* s) {
	return s->core->board;
}

static uint32_t bus32(struct GBA* gba, uint32_t a) {
	return gba->cpu->memory.load32(gba->cpu, a, NULL);
}

static uint8_t bus8(struct GBA* gba, uint32_t a) {
	return gba->cpu->memory.load8(gba->cpu, a, NULL);
}

static uint32_t rom32(const uint8_t* rom, size_t off) {
	return rom[off] | rom[off + 1] << 8 | rom[off + 2] << 16 | (uint32_t) rom[off + 3] << 24;
}

static bool inRam(uint32_t a) {
	return (a >= 0x02000000 && a < 0x02040000) || (a >= 0x03000000 && a < 0x03008000);
}

static bool inRom(uint32_t a, size_t romSize) {
	return a >= ROM_BASE && a - ROM_BASE < romSize;
}

bool RealtimeBgmStreamLocate(const uint8_t* rom, size_t romSize, struct RealtimeBgmStream* out) {
	for (size_t off = 0; off + 24 <= romSize; off += 4) {
		if (memcmp(rom + off, TAG, 8)) {
			continue;
		}
		struct RealtimeBgmStream d = {
			.log = rom32(rom, off + 8),
			.apply = rom32(rom, off + 12),
			.frame = rom32(rom, off + 16),
			.musicLane = (uint16_t) (rom[off + 20] | rom[off + 21] << 8),
			.sfxLane = (uint16_t) (rom[off + 22] | rom[off + 23] << 8),
		};
		if (inRam(d.log) && inRom(d.apply, romSize) && inRom(d.frame, romSize) && (d.apply & 1) && (d.frame & 1)) {
			*out = d;
			return true;
		}
	}
	return false;
}

static bool shadowCall(struct Stream* m, struct RealtimeBgmShadow* s, uint32_t fn, uint32_t r0) {
	struct ARMCore* cpu = gbaOf(s)->cpu;
	cpu->halted = 0;
	cpu->cpsr.i = 1;
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	cpu->gprs[0] = r0;
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

// Keep the shadow's lane, mute the other: the player only ever sets SOUNDCNT_H whole at init.
static void keepLane(struct Stream* m, struct RealtimeBgmShadow* s) {
	struct GBA* g = gbaOf(s);
	uint16_t mute = s->sfx ? m->d.musicLane : m->d.sfxLane;
	uint16_t cnt = g->memory.io[GBA_REG(SOUNDCNT_HI)];
	if (cnt & mute) {
		GBAIOWrite(g, GBA_REG_SOUNDCNT_HI, cnt & ~mute);
	}
}

// The commands main played since the last sync, then the player's frame.
static bool frame(void* state, struct RealtimeBgmShadow* s) {
	struct Stream* m = state;
	uint32_t count = bus32(m->mainGba, m->d.log + 4);
	uint32_t from = m->seen[s->sfx];
	if (count - from > LOG_N) { // a music lane far behind at high speed: the older ones are gone
		from = count - LOG_N;
	}
	for (; from != count; ++from) {
		if (!shadowCall(m, s, m->d.apply, bus8(m->mainGba, m->d.log + 8 + from % LOG_N))) {
			return false;
		}
	}
	m->seen[s->sfx] = count;
	keepLane(m, s);
	return shadowCall(m, s, m->d.frame, 0);
}

static bool ready(void* state, enum retro_bgm_status* fail) {
	struct Stream* m = state;
	(void) fail;
	return bus32(m->mainGba, m->d.log) == LOG_MAGIC;
}

// Parked: only our calls wake it. Cloned at the end of a main frame, after its VBlank work.
static void cloned(void* state, struct RealtimeBgmShadow* s) {
	struct Stream* m = state;
	struct GBA* g = gbaOf(s);
	struct ARMCore* cpu = g->cpu;
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	m->sp[s->sfx] = cpu->gprs[ARM_SP];
	g->memory.io[GBA_REG(IE)] = 0;
	g->memory.io[GBA_REG(IME)] = 0;
	GBAHalt(g);
	g->audio.masterVolume = m->mainGba->audio.masterVolume;
	m->seen[s->sfx] = bus32(m->mainGba, m->d.log + 4);
	keepLane(m, s);
}

// No tempo compensation to wait for.
static bool settled(void* state, const struct RealtimeBgmShadow* bgm, const struct RealtimeBgmShadow* sfx) {
	(void) state;
	(void) bgm;
	(void) sfx;
	return true;
}

static void observe(void* state) {
	(void) state;
}

bool RealtimeBgmStreamEngine(struct mCore* main, const struct RealtimeBgmStream* d, struct RealtimeBgmEngine* out) {
	struct Stream* m = calloc(1, sizeof(*m));
	if (!m) {
		return false;
	}
	m->main = main;
	m->mainGba = main->board;
	m->d = *d;
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
