/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../aux-core.h"
#include "../realtime-bgm.h"
#include "../realtime-bgm-engine.h"
#include "realtime-bgm-rom.h"

#include <mgba/core/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define CROSSFADE_FRAMES 12 // 200 ms at 60 Hz

static uint8_t rom[BGM_ROM_SIZE];
static mColor video[256 * 224];

static struct mCore* mainCore(void) {
	struct mCore* core = mCoreCreate(mPLATFORM_GBA);
	mCoreInitConfig(core, NULL);
	core->init(core);
	RetroAuxCoreConfigure(core);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->setVideoBuffer(core, video, 256);
	core->reset(core);
	return core;
}

// What m4aSoundInit leaves behind: the SoundInfo pointer, its ident, a chain
// of one player. Without the SoundInfo ident it is IWRAM that merely looks
// like a driver, as before the game's init.
static void soundState(struct mCore* core, bool ident) {
	core->busWrite32(core, 0x03007FF0, BGM_SOUND_INFO);
	core->busWrite32(core, BGM_SOUND_INFO, ident ? BGM_ID : 0);
	core->busWrite32(core, BGM_SOUND_INFO + 0x20, BGM_STUB_RETURN | 1); // MPlayMainHead
	core->busWrite32(core, BGM_SOUND_INFO + 0x24, BGM_PLAYER);
	core->busWrite32(core, BGM_PLAYER + 0x34, BGM_ID);
	core->busWrite32(core, BGM_PLAYER + 0x3C, 0);
	core->busWrite16(core, BGM_PLAYER + 0x1E, 0x100);
}

static void soundInit(struct mCore* core) {
	soundState(core, true);
}

// The player is playing the looping song: music from the first frame it is seen.
static void playSong(struct mCore* core) {
	core->busWrite32(core, BGM_PLAYER, BGM_SONG);
	core->busWrite32(core, BGM_PLAYER + 4, 1);
}

static void mainFrames(struct mCore* core, struct RealtimeBgm* bgm, int n) {
	for (int i = 0; i < n; ++i) {
		core->runFrame(core);
		RealtimeBgmAfterMainFrame(bgm);
	}
}

static struct RealtimeBgm* create(struct mCore* core, struct RealtimeBgmM4a m) {
	struct RealtimeBgmEngine engine;
	RealtimeBgmM4aEngine(core, rom, sizeof(rom), &m, &engine);
	return RealtimeBgmCreate(core, rom, sizeof(rom), &engine, 65536);
}

// Fast-forward held from the boot logo, before m4aSoundInit: take over once
// the driver is up, without another press.
static void takeover_waits_for_sound_init(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundState(core, false);
	mainFrames(core, bgm, 2);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(!RealtimeBgmActive(bgm));
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_OK);
	soundInit(core);
	mainFrames(core, bgm, 1);
	CHECK(RealtimeBgmActive(bgm));
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// The official BIOS plays its boot chime with its own m4a-like driver, ident
// and all: calling the game's driver on that state never returns, which fell
// back for the session when fast-forward was pressed on the boot logo.
static void bios_sound_driver_is_not_the_games(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	core->busWrite32(core, BGM_SOUND_INFO + 0x20, 0x2149); // as the BIOS leaves it
	mainFrames(core, bgm, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	mainFrames(core, bgm, 1);
	CHECK(!RealtimeBgmActive(bgm));
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_OK);
	soundInit(core);
	mainFrames(core, bgm, 1);
	CHECK(RealtimeBgmActive(bgm));
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// A driver call that never returns falls back for the session, without hanging.
static void budget_falls_back(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmM4a m = bgmStubs();
	m.soundMain = BGM_STUB_SPIN;
	struct RealtimeBgm* bgm = create(core, m);
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	mainFrames(core, bgm, 1);
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_BUDGET);
	CHECK(!RealtimeBgmActive(bgm));
	CHECK(RealtimeBgmLiveShadowsForTesting() == 0);
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(!RealtimeBgmActive(bgm));
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Switching the feature off mid fast-forward hands back through the crossfade.
static void disable_while_active_hands_back(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(RealtimeBgmActive(bgm));
	RealtimeBgmSetSpeed(bgm, 4, false);
	CHECK(RealtimeBgmActive(bgm));
	RealtimeBgmRun(bgm, 1);
	CHECK(RealtimeBgmActive(bgm));
	RealtimeBgmRun(bgm, CROSSFADE_FRAMES + 1);
	CHECK(!RealtimeBgmActive(bgm));
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Fast-forward again before the hand-back finished: the shadows carry on.
static void handback_reuses_shadow(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	unsigned clones = RealtimeBgmClonesForTesting();
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmRun(bgm, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(RealtimeBgmActive(bgm));
	CHECK(RealtimeBgmClonesForTesting() == clones);
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// The game still slows its music for the speed it saw (Mercury): keep the
// shadows until its tempo matches the shadow's again.
static void handback_waits_for_main_tempo(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	playSong(core);
	mainFrames(core, bgm, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	core->busWrite16(core, BGM_PLAYER + 0x1E, 64);
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmRun(bgm, 60);
	CHECK(RealtimeBgmActive(bgm));
	core->busWrite16(core, BGM_PLAYER + 0x1E, 0x100);
	RealtimeBgmRun(bgm, CROSSFADE_FRAMES + 2);
	CHECK(!RealtimeBgmActive(bgm));
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// The game also slows its sound effects for the speed it saw (Mercury, with
// no music playing): main's effects would come back low, so wait for those too.
static void handback_waits_for_effects_tempo(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	core->busWrite32(core, BGM_PLAYER, BGM_JINGLE);
	core->busWrite32(core, BGM_PLAYER + 4, 1);
	mainFrames(core, bgm, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	core->busWrite16(core, BGM_PLAYER + 0x1E, 64);
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmRun(bgm, 60);
	CHECK(RealtimeBgmActive(bgm));
	core->busWrite16(core, BGM_PLAYER + 0x1E, 0x100);
	RealtimeBgmRun(bgm, CROSSFADE_FRAMES + 2);
	CHECK(!RealtimeBgmActive(bgm));
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Cloned while main was inside a driver call (a lag frame): the driver's
// locks read ID+1, and no code on a shadow would ever release them, leaving
// SoundMain and MPlayStart returning early for the whole press.
static void clone_releases_driver_locks(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	core->busWrite32(core, BGM_SOUND_INFO, BGM_ID + 1);
	core->busWrite32(core, BGM_PLAYER + 0x34, BGM_ID + 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(RealtimeBgmActive(bgm));
	for (int sfx = 0; sfx < 2; ++sfx) {
		struct mCore* shadow = RealtimeBgmShadowForTesting(bgm, sfx);
		CHECK(shadow->busRead32(shadow, BGM_SOUND_INFO) == BGM_ID);
		CHECK(shadow->busRead32(shadow, BGM_PLAYER + 0x34) == BGM_ID);
	}
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Past 8x before m4aSoundInit the shadows are not in yet, but Dart already
// skipped the forced mute on the strength of the status: main's compressed
// audio must not play.
static void idle_past_8x_is_silent(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	RealtimeBgmSetSpeed(bgm, 16, true);
	CHECK(!RealtimeBgmActive(bgm));
	struct mAudioBuffer main;
	mAudioBufferInit(&main, 64, 2);
	int16_t loud[32 * 2];
	for (int i = 0; i < 64; ++i) loud[i] = 1000;
	mAudioBufferWrite(&main, loud, 32);
	struct mAudioBuffer* out = RealtimeBgmGameAudio(bgm, &main);
	int16_t got[32 * 2] = { 0 };
	CHECK(mAudioBufferRead(out, got, 32) == 32);
	CHECK(got[0] == 0 && got[63] == 0);
	mAudioBufferDeinit(&main);
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Main's level in what audio_batch plays after one main frame, fed a
// constant 1000 (the stub driver keeps the shadows silent).
static int16_t gameLaneLevel(struct mCore* core, struct RealtimeBgm* bgm) {
	mainFrames(core, bgm, 1);
	struct mAudioBuffer main;
	mAudioBufferInit(&main, 64, 2);
	int16_t loud[32 * 2];
	for (int i = 0; i < 64; ++i) loud[i] = 1000;
	mAudioBufferWrite(&main, loud, 32);
	int16_t got[32 * 2] = { 0 };
	mAudioBufferRead(RealtimeBgmGameAudio(bgm, &main), got, 32);
	mAudioBufferDeinit(&main);
	return got[0];
}

// Pressing again halfway through the hand-back fade ramps back to the
// shadows instead of snapping to them (a click).
static void repress_mid_fade_ramps_back(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmRun(bgm, 1 + CROSSFADE_FRAMES / 2);
	CHECK(gameLaneLevel(core, bgm) > 0);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(gameLaneLevel(core, bgm) > 0);
	RealtimeBgmRun(bgm, CROSSFADE_FRAMES);
	CHECK(gameLaneLevel(core, bgm) == 0);
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// A main core running a real BIOS (devices with one imported) saves states
// mGBA refuses to load into a core with another BIOS while the PC is inside
// it, which is where a game waiting for VBlank sits: the shadows must run the
// same BIOS, or every clone fails and they play nothing.
static void shadows_run_mains_bios(void) {
	static uint8_t bios[0x4000];
	for (int i = 0; i < 7; ++i) {
		bios[i * 4 + 3] = 0xEA; // b, as GBAIsBIOS wants
	}
	bios[0x100] = 0x5A;
	struct mCore* core = mainCore();
	core->loadBIOS(core, VFileFromConstMemory(bios, sizeof(bios)), 0);
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	// Built with the game, not on the first press: that took ~100 ms on a phone.
	CHECK(RealtimeBgmLiveShadowsForTesting() == 2);
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(RealtimeBgmActive(bgm));
	for (int sfx = 0; sfx < 2; ++sfx) {
		struct GBA* shadow = RealtimeBgmShadowForTesting(bgm, sfx)->board;
		CHECK(shadow->memory.fullBios);
		CHECK(shadow->biosChecksum == ((struct GBA*) core->board)->biosChecksum);
	}
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// The hand-back mix consumes main's whole frame even when the effects shadow
// produced less (a different sample rate): leftovers piled up for the length
// of the fade and then went out in one batch, past libretro.c's buffer.
static void fade_consumes_all_of_main(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmRun(bgm, 1 + CROSSFADE_FRAMES / 2);
	mainFrames(core, bgm, 1);
	struct mAudioBuffer* fx = RealtimeBgmShadowForTesting(bgm, true)->getAudioBuffer(RealtimeBgmShadowForTesting(bgm, true));
	size_t fxFrames = mAudioBufferAvailable(fx);
	struct mAudioBuffer main;
	mAudioBufferInit(&main, 0x1000, 2);
	static int16_t loud[0x1000 * 2];
	size_t mainFrames = fxFrames + 500;
	mAudioBufferWrite(&main, loud, mainFrames);
	struct mAudioBuffer* out = RealtimeBgmGameAudio(bgm, &main);
	CHECK(mAudioBufferAvailable(&main) == 0);
	CHECK(mAudioBufferAvailable(out) == mainFrames);
	mAudioBufferDeinit(&main);
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Mercury slows an effect for the speed it saw, so the effects shadow, at the
// game's own tempo, finishes first: it must not start the effect over while
// main still plays it, only when the game starts it again.
static void finished_effect_is_not_replayed(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmM4a m = bgmStubs();
	m.mplayStart = BGM_STUB_START;
	struct RealtimeBgm* bgm = create(core, m);
	soundInit(core);
	core->busWrite32(core, BGM_PLAYER, BGM_JINGLE);
	core->busWrite32(core, BGM_PLAYER + 4, 1);
	core->busWrite32(core, BGM_PLAYER + 0x0C, 5);
	mainFrames(core, bgm, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	struct mCore* fx = RealtimeBgmShadowForTesting(bgm, true);
	fx->busWrite32(fx, BGM_PLAYER + 4, 0);
	core->busWrite32(core, BGM_PLAYER + 0x0C, 6);
	mainFrames(core, bgm, 2);
	CHECK(fx->busRead32(fx, BGM_PLAYER + 4) == 0);
	// MPlayStart as a frame sees it: tracks off, clock 0; playing a frame later.
	core->busWrite32(core, BGM_PLAYER + 4, 0);
	core->busWrite32(core, BGM_PLAYER + 0x0C, 0);
	mainFrames(core, bgm, 1);
	core->busWrite32(core, BGM_PLAYER + 4, 1);
	core->busWrite32(core, BGM_PLAYER + 0x0C, 1);
	mainFrames(core, bgm, 1);
	CHECK(fx->busRead32(fx, BGM_PLAYER + 4) == 1);
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// An intro before any looping song (fast-forward at boot): no effect in the
// m4a games measured has more than 3 tracks, and their non-looping music 5 to 16.
static void many_track_song_is_music(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgmM4a m = bgmStubs();
	m.mplayStop = BGM_STUB_STOP;
	struct RealtimeBgm* bgm = create(core, m);
	soundInit(core);
	core->busWrite32(core, BGM_PLAYER, BGM_INTRO);
	core->busWrite32(core, BGM_PLAYER + 4, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
	mainFrames(core, bgm, 1);
	RealtimeBgmRun(bgm, 1);
	struct mCore* music = RealtimeBgmShadowForTesting(bgm, false);
	struct mCore* fx = RealtimeBgmShadowForTesting(bgm, true);
	CHECK(music->busRead32(music, BGM_PLAYER + 4) == 1);
	CHECK(fx->busRead32(fx, BGM_PLAYER + 4) == 0);
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
}

// Unloading mid hand-back frees both shadows (ASan reports a leak or a UAF).
static void destroy_mid_handback(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	mainFrames(core, bgm, 2);
	RealtimeBgmRun(bgm, 2);
	RealtimeBgmSetSpeed(bgm, 1, true);
	RealtimeBgmRun(bgm, 1);
	CHECK(RealtimeBgmLiveShadowsForTesting() == 2);
	RealtimeBgmDestroy(bgm);
	RealtimeBgmDestroy(NULL);
	CHECK(RealtimeBgmLiveShadowsForTesting() == 0);
	RetroAuxCoreDestroy(core);
}

// A GPIO cart (Ruby's RTC) made each shadow copy the whole ROM at reset and
// keep it for the session.
static void gpio_cart_shadows_share_rom(void) {
	uint8_t id[4];
	memcpy(id, &rom[0xAC], 4);
	memcpy(&rom[0xAC], "AXVE", 4);
	struct mCore* core = mainCore();
	CHECK(((struct GBA*) core->board)->memory.hw.devices & HW_RTC);
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundInit(core);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(RealtimeBgmActive(bgm));
	for (int sfx = 0; sfx < 2; ++sfx) {
		struct GBA* shadow = RealtimeBgmShadowForTesting(bgm, sfx)->board;
		CHECK((void*) shadow->memory.rom == rom);
	}
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(core);
	memcpy(&rom[0xAC], id, 4);
}

int main(void) {
	bgmBuildRom(rom);
	takeover_waits_for_sound_init();
	bios_sound_driver_is_not_the_games();
	budget_falls_back();
	disable_while_active_hands_back();
	handback_reuses_shadow();
	handback_waits_for_main_tempo();
	handback_waits_for_effects_tempo();
	clone_releases_driver_locks();
	idle_past_8x_is_silent();
	repress_mid_fade_ramps_back();
	shadows_run_mains_bios();
	fade_consumes_all_of_main();
	finished_effect_is_not_replayed();
	many_track_song_is_music();
	destroy_mid_handback();
	gpio_cart_shadows_share_rom();
	if (failures) {
		fprintf(stderr, "realtime-bgm-shadow: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-shadow: ok\n");
	return 0;
}
