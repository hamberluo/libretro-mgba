/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The realtime BGM glue in libretro.c for Pokémon GB, driven as a frontend does.

#include "../libretro_bgm.h"
#include "../realtime-bgm.h"
#include "../realtime-bgm-gb.h"
#include "realtime-bgm-gb-rom.h"

#include <mgba-util/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define GB_SAMPLES 512 // libretro.c's GB posting chunk

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

// What audio_batch received since the last reset. A waveform, not a level:
// with every DAC off the GB output settles on a DC offset.
static size_t heardFrames, heardCalls, largestCall;
static bool heardSound;
static size_t audio(const int16_t* data, size_t frames) {
	heardFrames += frames;
	++heardCalls;
	if (frames > largestCall) {
		largestCall = frames;
	}
	int lo = 0x7FFF, hi = -0x8000;
	for (size_t i = 0; i < frames * 2; ++i) {
		lo = data[i] < lo ? data[i] : lo;
		hi = data[i] > hi ? data[i] : hi;
	}
	heardSound |= frames && hi - lo > 64;
	return frames;
}
static void resetHeard(void) {
	heardFrames = heardCalls = largestCall = 0;
	heardSound = false;
}
static void poll(void) {}
static int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
	UNUSED(port);
	UNUSED(device);
	UNUSED(index);
	UNUSED(id);
	return 0;
}

static uint8_t rom[GB_ROM_SIZE];

static void load(bool engine) {
	static struct RealtimeBgmGb gb = { .family = REALTIME_BGM_GB_GEN2, .updateBank = 1, .updateSound = GB_STUB_ENGINE,
		                               .musicPlaying = 0xC100, .channelLength = 0x32, .channelFlags = 3 };
	RealtimeBgmGbSetLocateOverrideForTesting(engine ? &gb : NULL);
	struct retro_game_info info = { .path = NULL, .data = rom, .size = sizeof(rom) };
	CHECK(retro_load_game(&info));
	RealtimeBgmGbSetLocateOverrideForTesting(NULL);
	for (int f = 0; f < 10; ++f) {
		retro_run(); // deferred setup, then the cartridge starts its square wave
	}
}

static void runFrames(int n) {
	resetHeard();
	for (int f = 0; f < n; ++f) {
		retro_run();
	}
}

static void gb_status_ok_with_engine(void) {
	load(true);
	CHECK(retro_bgm_status() == RETRO_BGM_OK);
	retro_unload_game();
	load(false);
	CHECK(retro_bgm_status() == RETRO_BGM_GB_UNSUPPORTED);
	retro_unload_game();
}

// The effects shadow (DACs off, a stub engine) replaces main's square wave,
// frame for frame.
static void gb_main_audio_replaced_while_active(void) {
	load(true);
	runFrames(10);
	CHECK(heardSound);
	size_t idle = heardFrames;
	retro_bgm_set_speed(4, true);
	CHECK(retro_bgm_active());
	runFrames(10);
	CHECK(heardFrames + GB_SAMPLES >= idle && heardFrames <= idle + GB_SAMPLES);
	runFrames(2); // main's level decays once the DACs are off
	runFrames(8);
	CHECK(!heardSound);
	CHECK(largestCall <= GB_SAMPLES);
	retro_unload_game();
}

// No module, no change: libretro.c's own chunks.
static void gb_audio_unchanged_without_engine(void) {
	load(false);
	runFrames(10);
	CHECK(heardSound);
	CHECK(heardFrames == heardCalls * GB_SAMPLES);
	retro_unload_game();
}

static void gb_load_state_reclones(void) {
	load(true);
	size_t size = retro_serialize_size();
	void* state = malloc(size);
	CHECK(retro_serialize(state, size));
	retro_bgm_set_speed(4, true);
	unsigned clones = RealtimeBgmClonesForTesting();
	CHECK(retro_unserialize(state, size));
	CHECK(RealtimeBgmClonesForTesting() == clones + 1);
	CHECK(retro_bgm_active());
	free(state);
	retro_unload_game();
}

static void gb_past_8x_effects_silent(void) {
	load(true);
	retro_bgm_set_speed(16, true);
	runFrames(10);
	CHECK(heardFrames > 0);
	CHECK(!heardSound);
	retro_bgm_run(1);
	int16_t music[8192 * 2];
	CHECK(retro_bgm_read(music, 8192) > 0);
	retro_unload_game();
}

// The music lane comes at the rate the frontend was told for this content, and
// holds a catch-up of several frames (a stall) at GB's rate too.
static void gb_music_rate_matches_content(void) {
	load(true);
	struct retro_system_av_info av;
	retro_get_system_av_info(&av);
	retro_bgm_set_speed(4, true);
	retro_bgm_run(6);
	static int16_t music[0x10000 * 2];
	size_t frames = 0, n;
	while ((n = retro_bgm_read(music, 0x10000)) > 0) {
		frames += n;
	}
	double expected = av.timing.sample_rate * 6 / av.timing.fps;
	CHECK(frames > expected * 0.98 && frames < expected * 1.02);
	retro_unload_game();
}

int main(void) {
	gbBuildRom(rom, true);
	retro_set_environment(env);
	retro_init();
	retro_set_video_refresh(video);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll);
	retro_set_input_state(inputState);

	gb_status_ok_with_engine();
	gb_main_audio_replaced_while_active();
	gb_audio_unchanged_without_engine();
	gb_load_state_reclones();
	gb_past_8x_effects_silent();
	gb_music_rate_matches_content();
	retro_deinit();
	if (failures) {
		fprintf(stderr, "realtime-bgm-libretro-gb: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-libretro-gb: ok\n");
	return 0;
}
