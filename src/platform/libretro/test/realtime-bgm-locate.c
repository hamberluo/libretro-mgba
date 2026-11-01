/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../realtime-bgm.h"
#include "realtime-bgm-fixtures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// The fixture's slices pasted at their offsets into a zero ROM of `size` bytes,
// allocated exactly so ASan sees any read past the end.
static uint8_t* image(const struct BgmFixture* f, size_t size) {
	uint8_t* rom = calloc(1, size);
	for (size_t i = 0; i < f->count; ++i) {
		const struct BgmFixtureSlice* s = &f->slices[i];
		if (s->offset >= size) continue;
		size_t n = s->offset + s->length > size ? size - s->offset : s->length;
		memcpy(rom + s->offset, s->bytes, n);
	}
	return rom;
}

static const struct BgmFixture* fixture(const char* name) {
	for (size_t i = 0; i < sizeof(bgmFixtures) / sizeof(bgmFixtures[0]); ++i) {
		if (!strcmp(bgmFixtures[i].name, name)) return &bgmFixtures[i];
	}
	abort();
}

static void locate(const char* name, struct RealtimeBgmM4a* m, enum retro_bgm_status* status) {
	const struct BgmFixture* f = fixture(name);
	uint8_t* rom = image(f, f->romSize);
	*status = RealtimeBgmLocate(rom, f->romSize, m);
	free(rom);
}

// agbcc VSync/MPlayStart/MPlayStop as in most first-party games.
static void locate_agbcc_firered(void) {
	struct RealtimeBgmM4a m;
	enum retro_bgm_status st;
	locate("firered", &m, &st);
	CHECK(st == RETRO_BGM_OK);
	CHECK(m.soundMain == 0x081DC020);
	CHECK(m.soundVSync == 0x081DC9D4);
	CHECK(m.mplayStart == 0x081DD858);
	CHECK(m.mplayStop == 0x081DD93C);
}

// Minish Cap's VSync is C: the ident check is `adds rX, -ID`, not `cmp ID`.
static void locate_c_vsync_minish(void) {
	struct RealtimeBgmM4a m;
	enum retro_bgm_status st;
	locate("minish", &m, &st);
	CHECK(st == RETRO_BGM_OK);
	CHECK(m.soundVSync == 0x080B0674);
}

// gcc merges tempo 150 into the word 0x01000096 and schedules loads above
// MPlayStop's push.
static void locate_gcc_heart_and_soul(void) {
	struct RealtimeBgmM4a m;
	enum retro_bgm_status st;
	locate("heart_and_soul", &m, &st);
	CHECK(st == RETRO_BGM_OK);
	CHECK(m.mplayStart == 0x0817D3C4);
	CHECK(m.mplayStop == 0x0817D5FC);
}

// A data word that decodes as BL lands 4 bytes into MPlayStart.
static void locate_ignores_fake_bl_boktai(void) {
	struct RealtimeBgmM4a m;
	enum retro_bgm_status st;
	locate("boktai", &m, &st);
	CHECK(st == RETRO_BGM_OK);
	CHECK(m.mplayStart == 0x081D65A4);
	CHECK(m.mplayStop == 0x081D6688);
}

// Cut halfway into SoundMain's first literal (+0x6C): the signature still
// matches, so the literal read must be bounds-checked.
static void locate_truncated_rom(void) {
	const struct BgmFixture* f = fixture("firered");
	size_t size = f->soundMain - 0x08000000 + 0x6E;
	uint8_t* rom = image(f, size);
	struct RealtimeBgmM4a m;
	CHECK(RealtimeBgmLocate(rom, size, &m) == RETRO_BGM_NOT_M4A);
	free(rom);
}

int main(void) {
	locate_agbcc_firered();
	locate_c_vsync_minish();
	locate_gcc_heart_and_soul();
	locate_ignores_fake_bl_boktai();
	locate_truncated_rom();
	if (failures) {
		fprintf(stderr, "realtime-bgm-locate: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-locate: ok\n");
	return 0;
}
