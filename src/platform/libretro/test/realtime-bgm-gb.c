/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../aux-core.h"
#include "../realtime-bgm.h"
#include "../realtime-bgm-engine.h"
#include "../realtime-bgm-gb.h"
#include "realtime-bgm-gb-rom.h"

#include <mgba/core/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define MP 0xC100
#define LEN 0x32
#define IDS 0xC026 // gen1 wChannelSoundIDs
#define CH(c) (MP + 1 + (c) * LEN)

static uint8_t rom[GB_ROM_SIZE];
static mColor video[256 * 224];

static struct mCore* mainCore(void) {
	struct mCore* core = mCoreCreate(mPLATFORM_GB);
	mCoreInitConfig(core, NULL);
	core->init(core);
	RetroAuxCoreConfigure(core);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->setVideoBuffer(core, video, 256);
	core->reset(core);
	return core;
}

static struct RealtimeBgmGb gen2(void) {
	return (struct RealtimeBgmGb) { .family = REALTIME_BGM_GB_GEN2, .updateBank = 1, .updateSound = GB_STUB_ENGINE,
		                            .musicPlaying = MP, .channelLength = LEN, .channelFlags = 3 };
}

static struct RealtimeBgmGb gen1(void) {
	struct RealtimeBgmGb gb = { .family = REALTIME_BGM_GB_GEN1, .soundIds = IDS };
	gb.updateMusic[1] = GB_STUB_ENGINE;
	return gb;
}

static struct RealtimeBgm* create(struct mCore* core, struct RealtimeBgmGb gb) {
	struct RealtimeBgmEngine engine;
	RealtimeBgmGbEngine(core, &gb, &engine);
	return RealtimeBgmCreate(core, rom, sizeof(rom), &engine, 65536);
}

static void mainFrames(struct mCore* core, struct RealtimeBgm* bgm, int n) {
	for (int i = 0; i < n; ++i) {
		core->runFrame(core);
		RealtimeBgmAfterMainFrame(bgm);
	}
}

// gen2 channel c (0-based): on, with a music id.
static void channelOn(struct mCore* core, int c, uint8_t id) {
	core->busWrite8(core, MP, 1);
	core->busWrite8(core, CH(c), id);
	core->busWrite8(core, CH(c) + 3, core->busRead8(core, CH(c) + 3) | 1);
}

static bool channelIsOn(struct mCore* core, int c) {
	return core->busRead8(core, CH(c) + 3) & 1;
}

static struct mCore* shadow(struct RealtimeBgm* bgm, bool sfx) {
	return RealtimeBgmShadowForTesting(bgm, sfx);
}

static void done(struct RealtimeBgm* bgm, struct mCore* core) {
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

static void gen2_music_shadow_calls_engine(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	channelOn(core, 0, 0x10);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(RealtimeBgmActive(bgm));
	RealtimeBgmRun(bgm, 3);
	CHECK(shadow(bgm, false)->busRead8(shadow(bgm, false), GB_STUB_CALLS) == 3);
	done(bgm, core);
}

// An effect playing when main changes song must not play again at 1x.
static void gen2_music_sync_turns_effects_off(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	channelOn(core, 0, 0x10);
	channelOn(core, 4, 0x20);
	RealtimeBgmSetSpeed(bgm, 4, true);
	channelOn(core, 0, 0x11);
	RealtimeBgmRun(bgm, 1);
	struct mCore* music = shadow(bgm, false);
	CHECK(music->busRead8(music, CH(0)) == 0x11);
	CHECK(channelIsOn(music, 0));
	CHECK(!channelIsOn(music, 4));
	done(bgm, core);
}

static void gen2_effect_sync_turns_music_off(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	channelOn(core, 0, 0x10);
	RealtimeBgmSetSpeed(bgm, 4, true);
	channelOn(core, 4, 0x20);
	mainFrames(core, bgm, 1);
	struct mCore* fx = shadow(bgm, true);
	CHECK(channelIsOn(fx, 4));
	CHECK(fx->busRead8(fx, CH(4)) == 0x20);
	CHECK(!channelIsOn(fx, 0));
	done(bgm, core);
}

// Main's music was sounding at the clone; the effects shadow stops it.
static void effects_shadow_dacs_off_after_clone(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	core->busWrite8(core, 0xFF12, 0xF0);
	core->busWrite8(core, 0xFF17, 0xF0);
	core->busWrite8(core, 0xFF1A, 0x80);
	core->busWrite8(core, 0xFF21, 0xF0);
	channelOn(core, 0, 0x10);
	RealtimeBgmSetSpeed(bgm, 4, true);
	struct mCore* fx = shadow(bgm, true);
	CHECK(fx->busRead8(fx, 0xFF12) == 0);
	CHECK(fx->busRead8(fx, 0xFF17) == 0);
	CHECK(!(fx->busRead8(fx, 0xFF1A) & 0x80));
	CHECK(fx->busRead8(fx, 0xFF21) == 0);
	struct mCore* music = shadow(bgm, false);
	CHECK(music->busRead8(music, 0xFF12) == 0xF0);
	done(bgm, core);
}

// gen1 plays the music's drums as noise "effects" on channel 8 (ids 01-13).
static void gen1_drum_is_music(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen1());
	core->busWrite8(core, REALTIME_BGM_GB1_AUDIO_BANK, 1);
	core->busWrite8(core, IDS + 0, 0x01);
	core->busWrite8(core, IDS + 7, 0x05);
	RealtimeBgmSetSpeed(bgm, 4, true);
	mainFrames(core, bgm, 1);
	struct mCore* fx = shadow(bgm, true);
	struct mCore* music = shadow(bgm, false);
	CHECK(fx->busRead8(fx, IDS + 7) == 0);
	CHECK(music->busRead8(music, IDS + 7) == 0x05);
	core->busWrite8(core, IDS + 7, 0x20);
	mainFrames(core, bgm, 1);
	CHECK(fx->busRead8(fx, IDS + 7) == 0x20);
	core->busWrite8(core, IDS + 0, 0x02);
	RealtimeBgmRun(bgm, 1);
	CHECK(music->busRead8(music, IDS + 0) == 0x02);
	CHECK(music->busRead8(music, IDS + 7) == 0);
	done(bgm, core);
}

static void gen1_unknown_bank_skips(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen1());
	core->busWrite8(core, REALTIME_BGM_GB1_AUDIO_BANK, 2);
	core->busWrite8(core, IDS + 0, 0x01);
	RealtimeBgmSetSpeed(bgm, 4, true);
	RealtimeBgmRun(bgm, 2);
	CHECK(shadow(bgm, false)->busRead8(shadow(bgm, false), GB_STUB_CALLS) == 0);
	CHECK(RealtimeBgmActive(bgm));
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_OK);
	done(bgm, core);
}

static void gen1_home_banks_back_to_engine(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmGb gb = gen1();
	gb.updateMusic[1] = GB_STUB_HOME_CALL;
	struct RealtimeBgm* bgm = create(core, gb);
	core->busWrite8(core, REALTIME_BGM_GB1_AUDIO_BANK, 1);
	core->busWrite8(core, IDS + 0, 0x01);
	RealtimeBgmSetSpeed(bgm, 4, true);
	RealtimeBgmRun(bgm, 2);
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_OK);
	CHECK(shadow(bgm, false)->busRead8(shadow(bgm, false), GB_STUB_CALLS) == 2);
	done(bgm, core);
}

static void budget_falls_back(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmGb gb = gen2();
	gb.updateSound = GB_STUB_SPIN;
	struct RealtimeBgm* bgm = create(core, gb);
	channelOn(core, 0, 0x10);
	RealtimeBgmSetSpeed(bgm, 4, true);
	RealtimeBgmRun(bgm, 1);
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_BUDGET);
	CHECK(!RealtimeBgmActive(bgm));
	done(bgm, core);
}

// However long the engine runs, each 1x frame hands the lane one frame of
// samples: a jittery lane underruns the frontend's queue (pops).
static void music_lane_steady_when_engine_cost_varies(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmGb gb = gen2();
	gb.updateSound = GB_STUB_SLOW;
	struct RealtimeBgm* bgm = create(core, gb);
	channelOn(core, 0, 0x10);
	RealtimeBgmSetSpeed(bgm, 4, true);
	static int16_t pcm[4096 * 2];
	size_t lo = SIZE_MAX, hi = 0;
	for (int f = 0; f < 20; ++f) {
		RealtimeBgmRun(bgm, 1);
		size_t frames = 0, n;
		while ((n = RealtimeBgmRead(bgm, pcm, 4096)) > 0) {
			frames += n;
		}
		if (f >= 2) { // past the clone's sync frame
			lo = frames < lo ? frames : lo;
			hi = frames > hi ? frames : hi;
		}
	}
	CHECK(hi - lo <= 32); // the core's sample batching, nothing more
	done(bgm, core);
}

// The boot ROM covers bank 0 until the game writes FF50: no engine to call yet.
static void boot_rom_not_ready(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	struct GB* gb = core->board;
	gb->memory.io[GB_REG_BANK] = 0xFF;
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(!RealtimeBgmActive(bgm));
	mainFrames(core, bgm, 1);
	CHECK(!RealtimeBgmActive(bgm));
	gb->memory.io[GB_REG_BANK] = 0x01;
	mainFrames(core, bgm, 1);
	CHECK(RealtimeBgmActive(bgm));
	done(bgm, core);
}

// gen1 fades out on map changes from home code, straight into NR50.
static void nr50_mirrored(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	channelOn(core, 0, 0x10);
	RealtimeBgmSetSpeed(bgm, 4, true);
	core->busWrite8(core, 0xFF24, 0x33);
	RealtimeBgmRun(bgm, 1);
	CHECK(shadow(bgm, false)->busRead8(shadow(bgm, false), 0xFF24) == 0x33);
	done(bgm, core);
}

// Polished caches the loaded waveform: a sync that copies the record must
// bring the wave, or the shadow keeps the boot ROM's pattern.
static void sync_brings_wave(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	RealtimeBgmSetSpeed(bgm, 4, true);
	((struct GB*) core->board)->audio.ch3.wavedata8[0] = 0x13;
	channelOn(core, 0, 0x10);
	RealtimeBgmRun(bgm, 1);
	CHECK(((struct GB*) shadow(bgm, false)->board)->audio.ch3.wavedata8[0] == 0x13);
	done(bgm, core);
}

// Options load from the save after a clone at boot.
static void options_mirrored(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmGb gb = gen2();
	gb.options = 0xCFCC;
	struct RealtimeBgm* bgm = create(core, gb);
	RealtimeBgmSetSpeed(bgm, 4, true);
	core->busWrite8(core, 0xCFCC, 0x40);
	RealtimeBgmRun(bgm, 1);
	CHECK(shadow(bgm, false)->busRead8(shadow(bgm, false), 0xCFCC) == 0x40);
	done(bgm, core);
}

// gen1's map-change fade lives in home code no shadow runs: cloned mid-fade,
// the effects shadow kept main's near-silent master volume for the whole press.
static void effects_follow_main_volume(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen1());
	core->busWrite8(core, REALTIME_BGM_GB1_AUDIO_BANK, 1);
	core->busWrite8(core, IDS + 0, 0x01);
	core->busWrite8(core, 0xFF24, 0x00);
	RealtimeBgmSetSpeed(bgm, 4, true);
	core->busWrite8(core, 0xFF24, 0x77);
	mainFrames(core, bgm, 1);
	CHECK(shadow(bgm, true)->busRead8(shadow(bgm, true), 0xFF24) == 0x77);
	done(bgm, core);
}

// Some copy loops point SP into ROM (ld sp,hl): a call made from there would
// push into the MBC. Wait for a frame where SP is back in RAM.
static void sp_outside_ram_waits(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, gen2());
	channelOn(core, 0, 0x10);
	struct GB* gb = core->board;
	gb->cpu->sp = 0x4000;
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(!RealtimeBgmActive(bgm));
	gb->cpu->sp = 0xDFF0;
	mainFrames(core, bgm, 1);
	CHECK(RealtimeBgmActive(bgm));
	done(bgm, core);
}

int main(void) {
	gbBuildRom(rom, false);
	gen2_music_shadow_calls_engine();
	gen2_music_sync_turns_effects_off();
	gen2_effect_sync_turns_music_off();
	effects_shadow_dacs_off_after_clone();
	gen1_drum_is_music();
	gen1_unknown_bank_skips();
	gen1_home_banks_back_to_engine();
	budget_falls_back();
	music_lane_steady_when_engine_cost_varies();
	boot_rom_not_ready();
	nr50_mirrored();
	effects_follow_main_volume();
	sp_outside_ram_waits();
	sync_brings_wave();
	options_mirrored();
	if (failures) {
		fprintf(stderr, "realtime-bgm-gb: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-gb: ok\n");
	return 0;
}
