/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The libretro glue around link mode, driven the way a frontend drives it:
// SAVE_RAM keeps its address, saves are bounded, state-changing calls are
// refused while linked, and begin / end / unload cannot be misused into a
// use-after-free (needs ASan).

#include "../libretro_gogba.h"
#include "link-rom.h"

#include <mgba-util/common.h>

#include <stdio.h>
#include <string.h>

#ifndef GOGBA_LINK_CORE_ID
#define GOGBA_LINK_CORE_ID 0
#endif

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int framesShown;
// Pixels of the test ROM's backdrop in the last frame (this build is BGR555,
// no COLOR_5_6_5, the format LINK_GBA_BACKDROP is written in).
static int pixelsShown;

static bool env(unsigned cmd, void* data) {
	UNUSED(cmd);
	UNUSED(data);
	return false;
}
static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	framesShown += data != NULL;
	pixelsShown = 0;
	if (data) {
		unsigned x, y;
		for (y = 0; y < h; ++y) {
			for (x = 0; x < w; ++x) {
				pixelsShown += ((const uint16_t*) ((const uint8_t*) data + y * pitch))[x] == LINK_GBA_BACKDROP;
			}
		}
	}
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

static uint8_t rom[LINK_GBA_ROM_SIZE];

static void load(void) {
	struct retro_game_info info = { .path = NULL, .data = rom, .size = sizeof(rom) };
	CHECK(retro_load_game(&info), "retro_load_game refused the test ROM");
}

int main(void) {
	linkBuildGbaRom(rom);
	retro_set_environment(env);
	retro_init();
	retro_set_video_refresh(video);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll);
	retro_set_input_state(inputState);

	load();
	void* saveRam = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t saveSize = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	CHECK(retro_gogba_link_checksum() == 0, "checksum before linking is %08X", retro_gogba_link_checksum());

	// A peer with no save, and one with more bytes than any GBA save.
	static uint8_t huge[0x40000];
	memset(huge, 0x5A, sizeof(huge));
	struct retro_gogba_link_player saves[2] = { { huge, sizeof(huge) }, { NULL, 0 } };
	CHECK(!retro_gogba_link_begin(3, 0, saves, 0), "3 players accepted");
	CHECK(!retro_gogba_link_begin(2, 2, saves, 0), "local player 2 of 2 accepted");
	CHECK(retro_gogba_link_begin(2, 0, saves, 1700000000000LL), "link_begin refused a loaded GBA game");
	CHECK(!retro_gogba_link_begin(2, 0, saves, 0), "a second link_begin was accepted");
	CHECK(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == saveRam, "SAVE_RAM moved when linking");
	CHECK(((uint8_t*) saveRam)[saveSize - 1] == 0x5A, "local save not copied into SAVE_RAM");

	int f;
	for (f = 0; f < 60; ++f) {
		uint16_t masks[2] = { 0, (uint16_t) (1 << 8) };
		retro_gogba_link_set_input(masks);
		retro_run();
	}
	CHECK(framesShown >= 59, "only %d frames shown while linked", framesShown);
	CHECK(pixelsShown > 0, "the frames shown while linked were never drawn by the local core");
	CHECK(retro_gogba_link_checksum() != 0, "checksum is 0 while linked");
	CHECK(retro_gogba_link_core_id() == GOGBA_LINK_CORE_ID, "core id export does not match the build");
	CHECK(retro_serialize_size() == 0, "serialize_size is %zu while linked", retro_serialize_size());
	static uint8_t state[0x100000];
	CHECK(!retro_serialize(state, sizeof(state)), "serialize succeeded while linked");
	CHECK(!retro_unserialize(state, sizeof(state)), "unserialize succeeded while linked");
	// Reset and cheats must not reach a linked game: after them, the state has
	// to equal a second run with the same input that never made the calls.
	retro_reset();
	retro_cheat_set(0, true, "02030004 00000000");
	uint16_t still[2] = { 0, 0 };
	retro_gogba_link_set_input(still);
	retro_run();
	uint32_t afterCalls = retro_gogba_link_checksum();

	retro_gogba_link_end();
	retro_gogba_link_end(); // second end is a no-op
	CHECK(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == saveRam, "SAVE_RAM moved when unlinking");
	framesShown = 0;
	for (f = 0; f < 30; ++f) {
		retro_run();
	}
	CHECK(framesShown >= 29, "single player after unlinking showed %d frames", framesShown);
	CHECK(retro_serialize_size() > 0, "serialize still refused after unlinking");

	// Unloading while linked must free everything once.
	CHECK(retro_gogba_link_begin(2, 1, saves, 0), "relinking refused");
	retro_unload_game();
	load();
	CHECK(retro_gogba_link_begin(2, 0, saves, 1700000000000LL), "link_begin refused the second session");
	for (f = 0; f < 60; ++f) {
		uint16_t masks[2] = { 0, (uint16_t) (1 << 8) };
		retro_gogba_link_set_input(masks);
		retro_run();
	}
	retro_gogba_link_set_input(still);
	retro_run();
	CHECK(retro_gogba_link_checksum() == afterCalls, "reset or cheat changed a linked game (%08X, untouched run %08X)",
	      afterCalls, retro_gogba_link_checksum());
	retro_unload_game();
	retro_deinit();

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
