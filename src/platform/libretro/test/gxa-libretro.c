/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GXA in libretro.c, driven the way a frontend drives it: matching on load,
// the pack beside the ROM, GXA mixed into audio_batch, DUCK on the game's own
// sound, and the frontend's volume.

#include "../libretro_gxa.h"
#include "../libretro_link.h"

#include <mgba/core/core.h>
#include <mgba-util/common.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct mCore* RetroCoreForTesting(void);

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
#define NEAR(got, want, tol) do { double g_ = (got), w_ = (want); if (fabs(g_ - w_) > (tol)) { fprintf(stderr, "%s:%d: %s = %f, want %f\n", __FILE__, __LINE__, #got, g_, w_); ++failures; } } while (0)

#define REG(off) (0x04FFFA00 + (off))
#define FULL (0.5 / sqrt(2) * 32767)

static int16_t captured[48000 * 4];
static size_t nCaptured;

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
	size_t i;
	for (i = 0; i < frames && nCaptured < sizeof(captured) / sizeof(*captured); ++i) {
		captured[nCaptured++] = data[i * 2];
	}
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

#define ROM_SIZE 0x80000 // past EWRAM's size: not a multiboot image
static uint8_t rom[ROM_SIZE];

static void makeRom(const char* code) {
	memset(rom, 0, sizeof(rom));
	memcpy(rom, "\xFE\xFF\xFF\xEA", 4); // b .
	memcpy(&rom[0xA0], "GXA TEST", 8);
	memcpy(&rom[0xAC], code, 4);
	rom[0xB2] = 0x96;
}

static char fixture[1024];

static void load(const char* code) {
	makeRom(code);
	struct retro_game_info info = { .path = NULL, .data = rom, .size = sizeof(rom) };
	CHECK(retro_load_game(&info));
	retro_run();
}

static double rms(const int16_t* x, size_t n) {
	double s = 0;
	size_t i;
	for (i = 0; i < n; ++i) {
		s += (double) x[i] * x[i];
	}
	return n ? sqrt(s / n) : 0;
}

static double freq(const int16_t* x, size_t n, unsigned rate) {
	size_t i, crossings = 0;
	for (i = 1; i < n; ++i) {
		crossings += (x[i - 1] < 0) != (x[i] < 0);
	}
	return crossings / 2.0 * rate / n;
}

static unsigned outRate(void) {
	struct retro_system_av_info info;
	retro_get_system_av_info(&info);
	return (unsigned) info.timing.sample_rate;
}

// Captures frames of output after `skip` frames.
static size_t runCapture(int skip, int frames) {
	int i;
	for (i = 0; i < skip; ++i) {
		retro_run();
	}
	nCaptured = 0;
	for (i = 0; i < frames; ++i) {
		retro_run();
	}
	return nCaptured;
}

static void play(int track, int vol) {
	struct mCore* core = RetroCoreForTesting();
	core->busWrite16(core, REG(0x14), vol);
	core->busWrite16(core, REG(0x10), track);
	core->busWrite16(core, REG(0x12), 1);
}

static void gxa_libretro_load(void) {
	load("GXAT");
	CHECK(retro_gxa_load(fixture) == RETRO_GXA_HEADER_ONLY);
	CHECK(retro_gxa_status() == RETRO_GXA_HEADER_ONLY);
	char title[64], version[16];
	CHECK(retro_gxa_pack_info(title, sizeof(title), version, sizeof(version)) && !strcmp(title, "GXA test fixture"));
	retro_unload_game();
	load("ZZZZ");
	CHECK(retro_gxa_load(fixture) == RETRO_GXA_NO_MATCH);
	struct mCore* core = RetroCoreForTesting();
	core->busWrite16(core, REG(4), 0x1234);
	CHECK(core->busRead16(core, REG(4)) == (0x1234 ^ 0x5847));   // GXA without a pack still answers
	CHECK(core->busRead16(core, REG(8)) == 0x0400);
	retro_unload_game();
}

static void gxa_libretro_mix(void) {
	load("GXAT");
	CHECK(retro_gxa_load(fixture) == RETRO_GXA_HEADER_ONLY);
	play(1, 256);
	size_t n = runCapture(2, 20);
	CHECK(n > 4000);
	NEAR(freq(captured, n, outRate()), 440, 440 * 0.02);
	NEAR(rms(captured, n), FULL, FULL * 0.05);
	retro_unload_game();
}

static void gxa_libretro_volume(void) {
	load("GXAT");
	retro_gxa_load(fixture);
	retro_gxa_set_volume(0.5f);
	play(1, 256);
	size_t n = runCapture(2, 20);
	NEAR(rms(captured, n), FULL * 0.5, FULL * 0.5 * 0.03);
	retro_gxa_set_volume(1.0f);
	retro_unload_game();
}

// PSG channel 1: a 440 Hz square at full envelope, as the game's own sound.
static void gameTone(struct mCore* core) {
	core->busWrite16(core, 0x04000084, 0x0080);
	core->busWrite16(core, 0x04000080, 0x1177);
	core->busWrite16(core, 0x04000082, 0x0002);
	core->busWrite16(core, 0x04000062, 0xF080);
	core->busWrite16(core, 0x04000064, 0x86D6);
}

static void gxa_libretro_duck(void) {
	load("GXAT");
	retro_gxa_load(fixture);
	struct mCore* core = RetroCoreForTesting();
	gameTone(core);
	size_t n = runCapture(4, 10);
	double game = rms(captured, n);
	CHECK(game > 500);
	core->busWrite16(core, REG(0xC), 64);
	play(1, 0);   // a channel plays, silent: only the duck shows
	n = runCapture(2, 10);
	NEAR(rms(captured, n), game / 4, game / 4 * 0.05);
	retro_unload_game();
}

// a console reset puts every register back: GAMEVOL 256, channels idle
static void gxa_libretro_reset(void) {
	load("GXAT");
	retro_gxa_load(fixture);
	struct mCore* core = RetroCoreForTesting();
	core->busWrite16(core, REG(0xA), 0);
	play(1, 100);
	retro_run();
	retro_reset();
	CHECK(core->busRead16(core, REG(0xA)) == 256);
	CHECK(core->busRead16(core, REG(0x14)) == 256);
	CHECK(core->busRead16(core, REG(0x18)) == 0);
	CHECK(core->busRead16(core, REG(8)) == 0x0401);
	retro_unload_game();
}

static void gxa_libretro_autoload(void) {
	char dir[] = "/tmp/gxa-libretro-XXXXXX";
	CHECK(mkdtemp(dir));
	char romPath[256], packPath[256];
	snprintf(romPath, sizeof(romPath), "%s/x.gba", dir);
	snprintf(packPath, sizeof(packPath), "%s/x.gxa", dir);
	makeRom("GXAT");
	FILE* f = fopen(romPath, "wb");
	fwrite(rom, 1, sizeof(rom), f);
	fclose(f);
	FILE* in = fopen(fixture, "rb");
	f = fopen(packPath, "wb");
	char buf[4096];
	size_t r;
	while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
		fwrite(buf, 1, r, f);
	}
	fclose(in);
	fclose(f);
	struct retro_game_info info = { .path = romPath, .data = NULL, .size = 0 };
	CHECK(retro_load_game(&info));
	retro_run();
	struct mCore* core = RetroCoreForTesting();
	CHECK(core->busRead16(core, REG(8)) == 0x0401);
	CHECK(retro_gxa_status() == RETRO_GXA_HEADER_ONLY);
	retro_unload_game();
	unlink(romPath);
	unlink(packPath);
	rmdir(dir);
}

// The SHA-1 is the file's: a ROM whose size is not a power of two is padded
// in memory, and the padding must not change the hash (sha1-odd.gxa).
static void gxa_libretro_sha1_unpadded(void) {
	static uint8_t odd[0x80100];
	makeRom("GXAT");
	memcpy(odd, rom, sizeof(rom));
	struct retro_game_info info = { .path = NULL, .data = odd, .size = sizeof(odd) };
	CHECK(retro_load_game(&info));
	char pack[1024];
	snprintf(pack, sizeof(pack), "%.*s/sha1-odd.gxa", (int) (strrchr(fixture, '/') - fixture), fixture);
	CHECK(retro_gxa_load(pack) == RETRO_GXA_OK);
	retro_unload_game();
}

// The linked machine has no GXA: a pack mid-track must not keep ducking it.
static void gxa_libretro_link(void) {
	load("GXAT");
	retro_gxa_load(fixture);
	play(1, 100);
	retro_run();
	struct retro_link_player saves[2] = { { NULL, 0 }, { NULL, 0 } };
	CHECK(retro_link_begin(2, 0, saves, 0));
	CHECK(retro_gxa_status() == RETRO_GXA_NONE);
	retro_link_end();
	retro_unload_game();
}

int main(void) {
	const char* slash = strrchr(__FILE__, '/');
	snprintf(fixture, sizeof(fixture), "%.*s/../../../gba/test/gxa/fixture.gxa", (int) (slash - __FILE__), __FILE__);
	retro_set_environment(env);
	retro_set_video_refresh(video);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll);
	retro_set_input_state(inputState);
	retro_init();
	gxa_libretro_load();
	gxa_libretro_mix();
	gxa_libretro_volume();
	gxa_libretro_duck();
	gxa_libretro_reset();
	gxa_libretro_autoload();
	gxa_libretro_link();
	gxa_libretro_sha1_unpadded();
	retro_deinit();
	printf("gxa-libretro: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
