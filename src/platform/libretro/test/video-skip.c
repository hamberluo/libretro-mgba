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
static uint8_t sgbRom[LINK_GB_ROM_SIZE];

// An SGB cart that sends PAL_TRN over a solid screen, waits ~15 frames, then
// PAL_SET. The SGB reads the transfer back from the drawn picture.
static void buildSgbPalTrnRom(uint8_t out[LINK_GB_ROM_SIZE]) {
	static const uint8_t main[] = {
		0x31, 0xFE, 0xFF, 0xAF, 0xE0, 0x40, 0x21, 0x00, 0x80,
		0x3E, 0xFF, 0x22, 0x7C, 0xFE, 0x90, 0x20, 0xF8, 0x3E, 0x91, 0xE0, 0x40,
		0x21, 0x00, 0x03, 0xCD, 0x00, 0x02,
		0x06, 0xFF, 0x0E, 0x00, 0x0D, 0x20, 0xFD, 0x05, 0x20, 0xF8,
		0x21, 0x10, 0x03, 0xCD, 0x00, 0x02,
		0x18, 0xFE,
	};
	// Sends the 16-byte packet at hl through P14/P15.
	static const uint8_t send[] = {
		0xAF, 0xE0, 0x00, 0x3E, 0x30, 0xE0, 0x00, 0x06, 0x10,
		0x2A, 0x57, 0x0E, 0x08,
		0x7A, 0xE6, 0x01, 0x3E, 0x10, 0x20, 0x02, 0x3E, 0x20,
		0xE0, 0x00, 0x3E, 0x30, 0xE0, 0x00, 0xCB, 0x3A, 0x0D, 0x20, 0xEC,
		0x05, 0x20, 0xE5,
		0x3E, 0x20, 0xE0, 0x00, 0x3E, 0x30, 0xE0, 0x00, 0xC9,
	};
	linkBuildGbRom(out, false);
	memset(&out[0x150], 0, LINK_GB_ROM_SIZE - 0x150);
	memcpy(&out[0x150], main, sizeof(main));
	memcpy(&out[0x200], send, sizeof(send));
	out[0x300] = 0x0B << 3 | 1; // PAL_TRN
	out[0x310] = 0x0A << 3 | 1; // PAL_SET
	out[0x146] = 0x03;          // SGB functions
	out[0x14B] = 0x33;
	uint8_t check = 0;
	int i;
	for (i = 0x134; i < 0x14D; ++i) {
		check = check - out[i] - 1;
	}
	out[0x14D] = check;
}

// Runs `frames` from a fresh load, drawing only the last when `skip` is set.
static void play(const uint8_t* image, size_t size, unsigned frames, bool skip) {
	struct retro_game_info info = { .path = NULL, .data = image, .size = size };
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
	play(rom, sizeof(rom), 8, false);
	CHECK(!gotNull, "a frame with video enabled came back empty");
	size_t drawnSize = frameSize;
	memcpy(drawn, frame, sizeof(frame));

	play(rom, sizeof(rom), 8, true);
	CHECK(gotNull, "a frame with video disabled was still handed over");
	CHECK(frameSize == drawnSize && memcmp(frame, drawn, drawnSize) == 0,
	      "the frame after skipped ones differs from one drawn every frame");

	buildSgbPalTrnRom(sgbRom);
	play(sgbRom, sizeof(sgbRom), 40, false);
	drawnSize = frameSize;
	memcpy(drawn, frame, sizeof(frame));
	play(sgbRom, sizeof(sgbRom), 40, true);
	CHECK(frameSize == drawnSize && memcmp(frame, drawn, drawnSize) == 0,
	      "skipping frames lost an SGB palette transfer");

	retro_deinit();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
