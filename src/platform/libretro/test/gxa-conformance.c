/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The conformance ROM (gba-gxa's test-rom, built copies in src/gba/test/gxa) passes every case on this core, with its
// pack picked up beside it as a RetroArch user would place it.

#include "../libretro_gxa.h"

#include <mgba/core/core.h>
#include <mgba-util/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mCore* RetroCoreForTesting(void);

static bool env(unsigned cmd, void* data) {
	UNUSED(cmd);
	UNUSED(data);
	return false;
}
// GXA_SHOT=<file.ppm>: the last frame, for the docs
static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	const char* shot = getenv("GXA_SHOT");
	FILE* f = shot && data ? fopen(shot, "wb") : NULL;
	if (!f) {
		return;
	}
	fprintf(f, "P6 %u %u 255\n", w, h);
	unsigned x, y;
	for (y = 0; y < h; ++y) {
		const uint16_t* line = (const uint16_t*) ((const uint8_t*) data + y * pitch);
		for (x = 0; x < w; ++x) {
			uint16_t c = line[x];   // 0RGB1555: the environment never set another format
			uint8_t rgb[3] = { (uint8_t) (((c >> 10) & 0x1F) << 3), (uint8_t) (((c >> 5) & 0x1F) << 3), (uint8_t) ((c & 0x1F) << 3) };
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
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

#define RESULT 0x02000100

int main(void) {
	char path[1024];
	const char* slash = strrchr(__FILE__, '/');
	snprintf(path, sizeof(path), "%.*s/../../../gba/test/gxa/conformance.gba", (int) (slash - __FILE__), __FILE__);
	retro_set_environment(env);
	retro_set_video_refresh(video);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll);
	retro_set_input_state(inputState);
	retro_init();
	struct retro_game_info info = { .path = path, .data = NULL, .size = 0 };
	int failures = 0;
	if (!retro_load_game(&info)) {
		printf("FAIL cannot load %s\n", path);
		return 1;
	}
	if (retro_gxa_status() != RETRO_GXA_HEADER_ONLY) {
		printf("FAIL pack beside the ROM: status %d\n", retro_gxa_status());
		++failures;
	}
	struct mCore* core = RetroCoreForTesting();
	int frame;
	for (frame = 0; frame < 900 && core->busRead32(core, RESULT + 8) != 1; ++frame) {
		retro_run();
	}
	uint32_t magic = core->busRead32(core, RESULT), pass = core->busRead32(core, RESULT + 4);
	uint32_t done = core->busRead32(core, RESULT + 8), cases = core->busRead32(core, RESULT + 12);
	if (magic != 0x52415847 || done != 1 || cases != 9 || pass != 0x1FF) {
		printf("FAIL magic %08X done %u cases %u pass %02X after %d frames\n", magic, done, cases, pass, frame);
		++failures;
	}
	retro_unload_game();
	retro_deinit();
	printf("gxa-conformance: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
