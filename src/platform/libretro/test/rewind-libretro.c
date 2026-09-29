/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The rewind exports as a frontend drives them: every jump the player makes
// on purpose (reset, loading a state, starting link play) empties the ring, a
// linked game never captures, and unloading frees it.

#include "../libretro_link.h"
#include "../libretro_rewind.h"
#include "link-rom.h"

#include <mgba-util/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static bool env(unsigned cmd, void* data) {
	UNUSED(cmd);
	UNUSED(data);
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

static uint8_t rom[LINK_GBA_ROM_SIZE];

static void load(void) {
	struct retro_game_info info = { .path = NULL, .data = rom, .size = sizeof(rom) };
	CHECK(retro_load_game(&info), "retro_load_game refused the test ROM");
}

static void run(unsigned frames) {
	static const uint16_t still[2] = { 0, 0 };
	unsigned i;
	for (i = 0; i < frames; ++i) {
		retro_link_set_input(still); // ignored when not linked
		retro_run();
	}
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
	retro_rewind_configure(30);
	run(180);
	CHECK(retro_rewind_step(1) == 1, "no snapshot after 3 seconds");

	run(120);
	retro_reset();
	CHECK(retro_rewind_step(1) == 0, "reset kept the snapshots from before it");

	static uint8_t state[0x100000];
	size_t size = retro_serialize_size();
	CHECK(size > 0 && size <= sizeof(state) && retro_serialize(state, size), "serialize failed");
	run(120);
	CHECK(retro_unserialize(state, size), "unserialize failed");
	CHECK(retro_rewind_step(1) == 0, "loading a state kept the snapshots from before it");

	run(120);
	struct retro_link_player players[2] = { { NULL, 0, NULL }, { NULL, 0, NULL } };
	CHECK(retro_link_begin(2, 0, players, 0), "link_begin refused a loaded GBA game");
	CHECK(retro_rewind_step(1) == 0, "rewound into the machine link_begin replaced");
	run(120);
	CHECK(retro_rewind_step(1) == 0, "a linked game captured snapshots");
	retro_link_end();
	CHECK(retro_rewind_step(1) == 0, "snapshots from the linked session survived link_end");
	run(120);
	CHECK(retro_rewind_step(1) == 1, "capture did not resume after link_end");

	retro_unload_game();
	load();
	run(120);
	CHECK(retro_rewind_step(1) == 0, "the ring outlived retro_unload_game");
	retro_unload_game();

	retro_deinit();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
