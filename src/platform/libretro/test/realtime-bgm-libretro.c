/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The realtime BGM glue in libretro.c, driven the way a frontend drives it:
// status per platform, recloning when main's state is replaced, and teardown
// through unload or straight through deinit (needs ASan plus the shadow
// counter: macOS ASan does not report leaks).

#include "../libretro_bgm.h"
#include "../libretro_rewind.h"
#include "../realtime-bgm.h"
#include "link-rom.h"
#include "realtime-bgm-rom.h"

#include <mgba-util/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint8_t* iwram;

static bool env(unsigned cmd, void* data) {
	if (cmd == RETRO_ENVIRONMENT_SET_MEMORY_MAPS) {
		const struct retro_memory_map* maps = data;
		for (unsigned i = 0; i < maps->num_descriptors; ++i) {
			if (maps->descriptors[i].start == 0x03000000) {
				iwram = maps->descriptors[i].ptr;
			}
		}
		return true;
	}
	return false;
}
static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	UNUSED(data);
	UNUSED(w);
	UNUSED(h);
	UNUSED(pitch);
}
static size_t audio(const int16_t* data, size_t frames) {
	UNUSED(data);
	return frames;
}
static void poll(void) {}
static int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
	UNUSED(port);
	UNUSED(device);
	UNUSED(index);
	UNUSED(id);
	return 0;
}

static uint8_t bgmRom[BGM_ROM_SIZE];

static void put32(uint32_t address, uint32_t value) {
	memcpy(&iwram[address - 0x03000000], &value, 4);
}

static void loadBgmRom(const struct RealtimeBgmM4a* stubs) {
	RealtimeBgmSetLocateOverrideForTesting(stubs);
	struct retro_game_info info = { .path = NULL, .data = bgmRom, .size = sizeof(bgmRom) };
	CHECK(retro_load_game(&info));
	RealtimeBgmSetLocateOverrideForTesting(NULL);
	retro_run(); // deferred setup
}

// The test ROM with m4a stubs, its driver state written, fast-forwarding.
static void loadActive(void) {
	static struct RealtimeBgmM4a stubs;
	stubs = bgmStubs();
	loadBgmRom(&stubs);
	CHECK(iwram != NULL);
	put32(0x03007FF0, BGM_SOUND_INFO);
	put32(BGM_SOUND_INFO, BGM_ID);
	put32(BGM_SOUND_INFO + 0x20, BGM_STUB_RETURN | 1); // MPlayMainHead
	put32(BGM_SOUND_INFO + 0x24, BGM_PLAYER);
	put32(BGM_PLAYER + 0x34, BGM_ID);
	retro_bgm_set_speed(4, true);
	CHECK(retro_bgm_active());
}

static void status_gb_is_not_gba(void) {
	static uint8_t gbRom[LINK_GB_ROM_SIZE];
	linkBuildGbRom(gbRom, false);
	struct retro_game_info info = { .path = NULL, .data = gbRom, .size = sizeof(gbRom) };
	CHECK(retro_load_game(&info));
	CHECK(retro_bgm_status() == RETRO_BGM_NOT_GBA);
	retro_bgm_set_speed(4, true);
	CHECK(!retro_bgm_active());
	retro_unload_game();
}

static void status_cart_without_driver_is_not_m4a(void) {
	loadBgmRom(NULL);
	CHECK(retro_bgm_status() == RETRO_BGM_NOT_M4A);
	retro_bgm_set_speed(4, true);
	CHECK(!retro_bgm_active());
	retro_unload_game();
	CHECK(retro_bgm_status() == RETRO_BGM_NO_GAME);
}

static void unserialize_while_active_reclones(void) {
	loadActive();
	size_t size = retro_serialize_size();
	void* state = malloc(size);
	CHECK(retro_serialize(state, size));
	unsigned clones = RealtimeBgmClonesForTesting();
	CHECK(retro_unserialize(state, size));
	CHECK(RealtimeBgmClonesForTesting() == clones + 1);
	CHECK(retro_bgm_active());
	free(state);
	retro_unload_game();
}

static void rewind_while_active_reclones(void) {
	retro_rewind_configure(5);
	loadActive();
	int f;
	for (f = 0; f < 120; ++f) {
		retro_run();
	}
	unsigned clones = RealtimeBgmClonesForTesting();
	CHECK(retro_rewind_step(1) > 0);
	CHECK(RealtimeBgmClonesForTesting() == clones + 1);
	retro_unload_game();
	retro_rewind_configure(0);
}

// A reset lands before m4aSoundInit: the shadows step aside until the next
// press instead of replacing main's audio with a driver that never starts.
static void reset_while_active_hands_back(void) {
	loadActive();
	retro_reset();
	CHECK(!retro_bgm_active());
	retro_unload_game();
}

static void unload_while_active_frees_shadows(void) {
	loadActive();
	retro_run();
	retro_bgm_run(2);
	CHECK(RealtimeBgmLiveShadowsForTesting() == 2);
	retro_unload_game();
	CHECK(RealtimeBgmLiveShadowsForTesting() == 0);
	CHECK(retro_bgm_status() == RETRO_BGM_NO_GAME);
}

// A frontend may deinit without unloading first.
static void deinit_without_unload_frees_shadows(void) {
	loadActive();
	retro_run();
	CHECK(RealtimeBgmLiveShadowsForTesting() == 2);
	retro_deinit();
	CHECK(RealtimeBgmLiveShadowsForTesting() == 0);
}

int main(void) {
	bgmBuildRom(bgmRom);
	retro_set_environment(env);
	retro_init();
	retro_set_video_refresh(video);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll);
	retro_set_input_state(inputState);

	CHECK(retro_bgm_status() == RETRO_BGM_NO_GAME);
	status_gb_is_not_gba();
	status_cart_without_driver_is_not_m4a();
	unserialize_while_active_reclones();
	rewind_while_active_reclones();
	reset_while_active_hands_back();
	unload_while_active_frees_shadows();
	deinit_without_unload_frees_shadows();
	if (failures) {
		fprintf(stderr, "realtime-bgm-libretro: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-libretro: ok\n");
	return 0;
}
