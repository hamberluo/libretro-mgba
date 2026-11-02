/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../realtime-bgm-gb.h"
#include "realtime-bgm-fixtures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const struct GbFixture* fixture(const char* name) {
	for (size_t i = 0; i < sizeof(gbFixtures) / sizeof(gbFixtures[0]); ++i) {
		if (!strcmp(gbFixtures[i].name, name)) {
			return &gbFixtures[i];
		}
	}
	abort();
}

// The fixture's bytes at their offsets in a zero ROM of `size` (0: the original size).
static uint8_t* rom(const struct GbFixture* f, size_t size) {
	uint8_t* r = calloc(1, size ? size : f->romSize);
	for (size_t i = 0; i < f->count; ++i) {
		const struct BgmFixtureSlice* s = &f->slices[i];
		if (s->offset + s->length <= (size ? size : f->romSize)) {
			memcpy(r + s->offset, s->bytes, s->length);
		}
	}
	return r;
}

static void gen2(const char* name, uint16_t entry, uint16_t mp, uint8_t len, uint8_t flags, uint16_t options) {
	const struct GbFixture* f = fixture(name);
	uint8_t* r = rom(f, 0);
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_OK);
	CHECK(gb.family == REALTIME_BGM_GB_GEN2);
	CHECK(gb.updateBank == 0x3A);
	CHECK(gb.updateSound == entry);
	CHECK(gb.musicPlaying == mp);
	CHECK(gb.channelLength == len);
	CHECK(gb.channelFlags == flags);
	CHECK(gb.options == options);
	free(r);
}

static void gen2_crystal(void) {
	gen2("crystal", 0x405C, 0xC100, 0x32, 3, 0xCFCC);
}

static void gen2_polished(void) {
	gen2("polished", 0x40F1, 0xCB7E, 0x24, 7, 0xCFF1);
}

static void gen2_prism(void) {
	gen2("prism", 0x4051, 0xC100, 0x32, 3, 0xCFCC);
}

static void gen1_red(void) {
	const struct GbFixture* f = fixture("red");
	uint8_t* r = rom(f, 0);
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_OK);
	CHECK(gb.family == REALTIME_BGM_GB_GEN1);
	const uint8_t* sig = f->slices[0].bytes;
	CHECK(gb.soundIds == (sig[5] | sig[6] << 8));
	for (int bank = 0; bank < 256; ++bank) {
		CHECK(!gb.updateMusic[bank] == !(bank == 0x02 || bank == 0x08 || bank == 0x1F));
	}
	CHECK(gb.updateMusic[0x02] == 0x4000 + 0x9103 % 0x4000);
	free(r);
}

// Brown carries its own audio banks besides Red's.
static void gen1_brown(void) {
	const struct GbFixture* f = fixture("brown");
	uint8_t* r = rom(f, 0);
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_OK);
	CHECK(gb.updateMusic[0x7A]);
	CHECK(gb.updateMusic[0x02] && gb.updateMusic[0x08] && gb.updateMusic[0x1F]);
	free(r);
}

static void unknown_gb_unsupported(void) {
	uint8_t* r = calloc(1, 0x8000);
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, 0x8000, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
}

// Opening bytes that look right with parameters that cannot be the engine's.
static void gen2_bad_params_rejected(void) {
	const struct GbFixture* f = fixture("crystal");
	const uint32_t e = 0x3A * 0x4000 + 0x5C;
	struct RealtimeBgmGb gb;
	uint8_t* r = rom(f, 0);
	r[e + 13] += 1; // ld bc: wChannel1 != wMusicPlaying + 1
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
	static const uint8_t lengths[] = { 0x00, 0x41 };
	for (int i = 0; i < 2; ++i) {
		r = rom(f, 0);
		r[e + 0xA6 + 1] = lengths[i]; // ld hl, CHANNEL_STRUCT_LENGTH
		CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_GB_UNSUPPORTED);
		free(r);
	}
	r = rom(f, 0);
	r[e + 15 + 1] = 0x32; // ld hl, CHANNEL_FLAGS1: past the struct
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
}

// Cut right after the ld bc operand: the scan must stop at the end.
static void truncated_rom_in_bounds(void) {
	const struct GbFixture* f = fixture("crystal");
	size_t size = 0x3A * 0x4000 + 0x5C + 15;
	uint8_t* r = rom(f, size);
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, size, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
}

// gen1's id table must sit in WRAM, ahead of wAudioROMBank: the shadows copy
// C000-C0FF and write ids there.
static void gen1_ids_outside_wram_rejected(void) {
	const struct GbFixture* f = fixture("red");
	uint8_t* r = rom(f, 0);
	for (size_t i = 0; i < f->count; ++i) {
		r[f->slices[i].offset + 5] = 0x00;
		r[f->slices[i].offset + 6] = 0x40;
	}
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
}

// gen2's channels and globals, as the shadows copy them, must end before echo RAM.
static void gen2_globals_past_wram_rejected(void) {
	const struct GbFixture* f = fixture("crystal");
	const uint32_t e = 0x3A * 0x4000 + 0x5C;
	uint8_t* r = rom(f, 0);
	r[e + 1] = 0x00; r[e + 2] = 0xDF;   // wMusicPlaying = DF00
	r[e + 13] = 0x01; r[e + 14] = 0xDF; // wChannel1 = DF01
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, f->romSize, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
}

// Banks past 255 do not fit the bank number.
static void gen2_past_bank_255_rejected(void) {
	const struct GbFixture* f = fixture("crystal");
	const size_t size = 0x101 * 0x4000;
	uint8_t* r = calloc(1, size);
	memcpy(r + 0x100 * 0x4000 + 0x5C, f->slices[0].bytes, f->slices[0].length);
	struct RealtimeBgmGb gb;
	CHECK(RealtimeBgmGbLocate(r, size, &gb) == RETRO_BGM_GB_UNSUPPORTED);
	free(r);
}

int main(void) {
	gen2_crystal();
	gen2_polished();
	gen2_prism();
	gen1_red();
	gen1_brown();
	unknown_gb_unsupported();
	gen2_bad_params_rejected();
	truncated_rom_in_bounds();
	gen1_ids_outside_wram_rejected();
	gen2_globals_past_wram_rejected();
	gen2_past_bank_255_rejected();
	if (failures) {
		fprintf(stderr, "realtime-bgm-gb-locate: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-gb-locate: ok\n");
	return 0;
}
