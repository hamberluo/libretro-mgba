/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "realtime-bgm-gb.h"
#include "realtime-bgm-engine.h"

#include <mgba/core/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba/internal/gb/mbc.h>
#include <mgba/internal/sm83/sm83.h>

#include <stdlib.h>
#include <string.h>

#define CALL_BUDGET 4000000 // SM83 ticks
#define SENTINEL 0x0000     // return address: the reset vector, never a sound routine's caller
#define GEN1_RAM 0xC000
#define GEN1_RAM_END 0xC100
#define MUSIC_CHANNELS 4
#define CHANNELS 8
#define DRUM_CHANNEL 7

struct Gb {
	struct mCore* main;
	struct GB* mainGb;
	struct RealtimeBgmGb gb;
	uint16_t haltAt; // a HALT byte in bank 0: parked there with IE=0, the CPU never wakes
	// Per shadow, indexed by RealtimeBgmShadow.sfx.
	uint16_t sp[2];
	uint16_t lastId[2][CHANNELS];
	bool lastOn[2][CHANNELS];
};

static struct GB* gbOf(const struct RealtimeBgmShadow* s) {
	return s->core->board;
}

static uint8_t rd(struct GB* g, uint16_t a) {
	return g->cpu->memory.load8(g->cpu, a);
}

static void wr(struct GB* g, uint16_t a, uint8_t v) {
	g->cpu->memory.store8(g->cpu, a, v);
}

static uint16_t channel(const struct Gb* e, int c) {
	return e->gb.musicPlaying + 1 + c * e->gb.channelLength;
}

static uint16_t channelId(const struct Gb* e, struct GB* g, int c) {
	if (e->gb.family == REALTIME_BGM_GB_GEN1) {
		return rd(g, e->gb.soundIds + c);
	}
	return rd(g, channel(e, c)) | rd(g, channel(e, c) + 1) << 8;
}

static bool isDrum(const struct Gb* e, int c, uint16_t id) {
	return e->gb.family == REALTIME_BGM_GB_GEN1 && c == DRUM_CHANNEL && id <= REALTIME_BGM_GB1_DRUM_MAX;
}

// gen1 has no on flag: a channel plays while its id is set. Drums are music.
static bool channelOn(const struct Gb* e, struct GB* g, int c) {
	if (e->gb.family == REALTIME_BGM_GB_GEN1) {
		uint16_t id = channelId(e, g, c);
		return id && !(c >= MUSIC_CHANNELS && isDrum(e, c, id));
	}
	return rd(g, channel(e, c) + e->gb.channelFlags) & 1;
}

static void channelOff(const struct Gb* e, struct GB* g, int c) {
	if (e->gb.family == REALTIME_BGM_GB_GEN1) {
		wr(g, e->gb.soundIds + c, 0);
	} else {
		uint16_t flags = channel(e, c) + e->gb.channelFlags;
		wr(g, flags, rd(g, flags) & ~1);
	}
}

// Each shadow keeps only its own channels. gen1's drums ride on channel 8 but
// belong to the music.
static void keepOwnChannels(const struct Gb* e, const struct RealtimeBgmShadow* s) {
	struct GB* g = gbOf(s);
	for (int c = 0; c < CHANNELS; ++c) {
		bool music = c < MUSIC_CHANNELS || isDrum(e, c, channelId(e, g, c));
		if (music == s->sfx) {
			channelOff(e, g, c);
		}
	}
}

static void copySoundRam(const struct Gb* e, const struct RealtimeBgmShadow* s) {
	uint16_t from = GEN1_RAM, to = GEN1_RAM_END;
	if (e->gb.family == REALTIME_BGM_GB_GEN2) {
		from = e->gb.musicPlaying;
		to = channel(e, CHANNELS) + REALTIME_BGM_GB2_GLOBALS;
	}
	for (uint32_t a = from; a < to; ++a) {
		wr(gbOf(s), a, rd(e->mainGb, a));
	}
	// Engines that remember the loaded waveform (Polished's wCh3LoadedWaveform)
	// skip the reload: the wave must come with that record.
	memcpy(gbOf(s)->audio.ch3.wavedata8, e->mainGb->audio.ch3.wavedata8, sizeof(e->mainGb->audio.ch3.wavedata8));
}

// The music shadow follows main's music channels; the effects shadow, effects
// main starts. Updates the record either way.
static bool mainChanged(struct Gb* e, const struct RealtimeBgmShadow* s) {
	bool changed = false;
	int from = s->sfx ? MUSIC_CHANNELS : 0, to = s->sfx ? CHANNELS : MUSIC_CHANNELS;
	for (int c = from; c < to; ++c) {
		uint16_t id = channelId(e, e->mainGb, c);
		bool on = channelOn(e, e->mainGb, c);
		if (s->sfx ? on && (!e->lastOn[1][c] || id != e->lastId[1][c]) : on != e->lastOn[0][c] || id != e->lastId[0][c]) {
			changed = true;
		}
		e->lastId[s->sfx][c] = id;
		e->lastOn[s->sfx][c] = on;
	}
	return changed;
}

static void park(const struct Gb* e, struct SM83Core* cpu) {
	cpu->halted = false;
	cpu->irqPending = false;
	cpu->executionState = SM83_CORE_FETCH;
	cpu->pc = e->haltAt;
	cpu->memory.setActiveRegion(cpu, cpu->pc);
}

// A subroutine call with the bank switched in. False past the budget.
static bool call(const struct Gb* e, const struct RealtimeBgmShadow* s, uint8_t bank, uint16_t entry) {
	struct GB* g = gbOf(s);
	struct SM83Core* cpu = g->cpu;
	int saved = g->memory.currentBank;
	GBMBCSwitchBank(g, bank);
	uint16_t sp = e->sp[s->sfx];
	cpu->sp = sp - 2;
	wr(g, cpu->sp, SENTINEL & 0xFF);
	wr(g, cpu->sp + 1, SENTINEL >> 8);
	cpu->halted = false;
	cpu->irqPending = false;
	cpu->executionState = SM83_CORE_FETCH;
	cpu->pc = entry;
	cpu->memory.setActiveRegion(cpu, entry);
	bool returned = false;
	for (long budget = CALL_BUDGET; budget > 0 && !returned; --budget) {
		SM83Tick(cpu);
		returned = cpu->pc == SENTINEL && cpu->sp == sp && cpu->executionState == SM83_CORE_FETCH;
	}
	cpu->sp = sp;
	GBMBCSwitchBank(g, saved);
	park(e, cpu);
	return returned;
}

static bool callEngine(const struct Gb* e, const struct RealtimeBgmShadow* s) {
	if (e->gb.family == REALTIME_BGM_GB_GEN2) {
		return call(e, s, e->gb.updateBank, e->gb.updateSound);
	}
	// The shadow's own song: its copy of the bank came with the last sync.
	uint8_t bank = rd(gbOf(s), REALTIME_BGM_GB1_AUDIO_BANK);
	return !e->gb.updateMusic[bank] || call(e, s, bank, e->gb.updateMusic[bank]);
}

// The boot ROM covers bank 0 until the game writes FF50. Calls push below the
// clone's SP, which some copy loops point into ROM (ld sp,hl): wait for RAM.
static bool ready(void* state, enum retro_bgm_status* fail) {
	UNUSED(fail);
	struct Gb* e = state;
	uint16_t sp = e->mainGb->cpu->sp;
	bool spInRam = (sp >= GB_BASE_WORKING_RAM_BANK0 + 2 && sp < GB_BASE_WORKING_RAM_BANK0 + 0x2000) ||
	               (sp >= GB_BASE_HRAM + 2 && sp <= GB_BASE_HRAM + GB_SIZE_HRAM);
	return e->mainGb->memory.io[GB_REG_BANK] != 0xFF && spInRam;
}

static void cloned(void* state, struct RealtimeBgmShadow* s) {
	struct Gb* e = state;
	struct GB* g = gbOf(s);
	g->memory.ie = 0;
	g->memory.ime = false;
	e->sp[s->sfx] = g->cpu->sp;
	park(e, g->cpu);
	mainChanged(e, s);
	if (s->sfx) {
		// Main's music was sounding: DACs off until an effect turns one back on.
		wr(g, GB_BASE_IO | GB_REG_NR12, 0);
		wr(g, GB_BASE_IO | GB_REG_NR22, 0);
		wr(g, GB_BASE_IO | GB_REG_NR30, 0);
		wr(g, GB_BASE_IO | GB_REG_NR42, 0);
	}
	keepOwnChannels(e, s);
}

// After a sync main has already ticked the copied state this frame.
static bool frame(void* state, struct RealtimeBgmShadow* s) {
	struct Gb* e = state;
	// Loaded from the save after a boot clone.
	if (e->gb.options) {
		wr(gbOf(s), e->gb.options, rd(e->mainGb, e->gb.options));
	}
	if (mainChanged(e, s)) {
		copySoundRam(e, s);
		keepOwnChannels(e, s);
	} else if (!callEngine(e, s)) {
		return false;
	}
	// Shadows never run home code, where gen1 sets the master volume (its map
	// change fade): follow main's.
	wr(gbOf(s), GB_BASE_IO | GB_REG_NR50, rd(e->mainGb, GB_BASE_IO | GB_REG_NR50));
	return true;
}

// The engines never retime the music for the speed.
static bool settled(void* state, const struct RealtimeBgmShadow* bgm, const struct RealtimeBgmShadow* sfx) {
	UNUSED(state);
	UNUSED(bgm);
	UNUSED(sfx);
	return true;
}

static void observe(void* state) {
	UNUSED(state);
}

bool RealtimeBgmGbEngine(struct mCore* main, const struct RealtimeBgmGb* gb, struct RealtimeBgmEngine* out) {
	struct GB* g = main->board;
	uint16_t haltAt = 0x150;
	while (haltAt < GB_SIZE_CART_BANK0 && haltAt < g->memory.romSize && g->memory.rom[haltAt] != 0x76) {
		++haltAt;
	}
	if (haltAt >= GB_SIZE_CART_BANK0 || haltAt >= g->memory.romSize) {
		return false;
	}
	struct Gb* e = calloc(1, sizeof(*e));
	if (!e) {
		return false;
	}
	e->main = main;
	e->mainGb = g;
	e->gb = *gb;
	e->haltAt = haltAt;
	*out = (struct RealtimeBgmEngine) {
		.state = e,
		.ready = ready,
		.cloned = cloned,
		.frame = frame,
		.settled = settled,
		.observe = observe,
		.destroy = free,
	};
	return true;
}
