/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A frontend that clears the video bit of GET_AUDIO_VIDEO_ENABLE gets no
// frame for it, and the frame it does show next matches one drawn every time.

#include "../libretro_link.h"
#include "link-rom.h"

#include <mgba-util/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static bool videoEnabled = true;
static bool gotNull;
static uint8_t frame[256 * 160 * 4];
static size_t frameSize;

static bool env(unsigned cmd, void* data) {
	if (cmd == RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE) {
		*(int*) data = videoEnabled ? 3 : 2;
		return true;
	}
	return false;
}
static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	UNUSED(w);
	if (!data) {
		gotNull = true;
		return;
	}
	frameSize = h * pitch;
	memcpy(frame, data, frameSize);
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

// Runs `frames` from a fresh load, drawing only the last when `skip` is set.
static void play(unsigned frames, bool skip) {
	struct retro_game_info info = { .path = NULL, .data = rom, .size = sizeof(rom) };
	CHECK(retro_load_game(&info), "retro_load_game refused the test ROM");
	memset(frame, 0, sizeof(frame));
	frameSize = 0;
	gotNull = false;
	unsigned i;
	for (i = 0; i < frames; ++i) {
		videoEnabled = !skip || i == frames - 1;
		retro_run();
	}
	retro_unload_game();
}

int main(void) {
	linkBuildGbaRom(rom);
	retro_set_environment(env);
	retro_init();
	retro_set_video_refresh(video);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll);
	retro_set_input_state(inputState);

	static uint8_t drawn[sizeof(frame)];
	play(8, false);
	CHECK(!gotNull, "a frame with video enabled came back empty");
	size_t drawnSize = frameSize;
	memcpy(drawn, frame, sizeof(frame));

	play(8, true);
	CHECK(gotNull, "a frame with video disabled was still handed over");
	CHECK(frameSize == drawnSize && memcmp(frame, drawn, drawnSize) == 0,
	      "the frame after skipped ones differs from one drawn every frame");

	retro_deinit();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
