/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../aux-core.h"
#include "../realtime-bgm.h"
#include "realtime-bgm-rom.h"

#include <mgba/core/core.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>

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
	return RealtimeBgmCreate(core, rom, sizeof(rom), &m, 65536);
}

// m4aSoundInit has not run yet (boot logo): stay out this press, take over the next.
static void takeover_waits_for_sound_init(void) {
	struct mCore* core = mainCore();
	struct RealtimeBgm* bgm = create(core, bgmStubs());
	soundState(core, false);
	mainFrames(core, bgm, 2);
	RealtimeBgmSetSpeed(bgm, 4, true);
	CHECK(!RealtimeBgmActive(bgm));
	CHECK(RealtimeBgmStatus(bgm) == RETRO_BGM_OK);
	RealtimeBgmSetSpeed(bgm, 1, true);
	soundInit(core);
	mainFrames(core, bgm, 1);
	RealtimeBgmSetSpeed(bgm, 4, true);
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

int main(void) {
	bgmBuildRom(rom);
	takeover_waits_for_sound_init();
	budget_falls_back();
	disable_while_active_hands_back();
	handback_reuses_shadow();
	handback_waits_for_main_tempo();
	handback_waits_for_effects_tempo();
	clone_releases_driver_locks();
	idle_past_8x_is_silent();
	destroy_mid_handback();
	if (failures) {
		fprintf(stderr, "realtime-bgm-shadow: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-shadow: ok\n");
	return 0;
}
