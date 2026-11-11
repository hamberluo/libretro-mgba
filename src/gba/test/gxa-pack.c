/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GXA packs: store-only zips with manifest.json (src/gba/test/gxa/make_fixtures.py
// writes the fixtures). Track lookup, rejected packs, and ROM matching.

#include <mgba/internal/gba/gxa.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond); ++failures; } } while (0)

static struct GXAPack* openFixture(const char* name) {
	char path[1024];
	const char* here = __FILE__;
	const char* slash = strrchr(here, '/');
	snprintf(path, sizeof(path), "%.*s/gxa/%s", (int) (slash - here), here, name);
	const char* env = getenv("GXA_PACK");
	if (env && !strcmp(name, "fixture.gxa")) {
		snprintf(path, sizeof(path), "%s", env);
	}
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) {
		printf("FAIL cannot open %s\n", path);
		++failures;
		return NULL;
	}
	return GXAPackOpen(vf);
}

// The fixtures' ROM image: 192 zero bytes, title at 0xA0, game code at 0xAC.
static void testRom(uint8_t rom[192], const char* code) {
	memset(rom, 0, 192);
	memcpy(&rom[0xA0], "GXA TEST", 8);
	memcpy(&rom[0xAC], code, 4);
}

static void gxa_pack_tracks(void) {
	struct GXAPack* pack = openFixture("fixture.gxa");
	CHECK(pack);
	if (!pack) {
		return;
	}
	const void* data;
	size_t size;
	int32_t loop;
	float gain;
	CHECK(GXAPackTrack(pack, 1, &data, &size, &loop, &gain) && size > 0 && loop < 0 && gain == 0);
	CHECK(!memcmp(data, "OggS", 4));
	CHECK(GXAPackTrack(pack, 2, &data, &size, &loop, &gain) && loop == 12000);
	CHECK(GXAPackTrack(pack, 3, &data, &size, &loop, &gain) && fabsf(gain + 6) < 0.001f);
	CHECK(!GXAPackTrack(pack, 7, &data, &size, &loop, &gain));
	CHECK(!GXAPackTrack(pack, 4, &data, &size, &loop, &gain));
	CHECK(!strcmp(GXAPackTitle(pack), "GXA test fixture"));
	GXAPackClose(pack);
}

static void gxa_pack_rejects(void) {
	CHECK(!openFixture("bad-deflate.gxa"));
	CHECK(!openFixture("bad-spec.gxa"));
}

static void gxa_pack_match(void) {
	uint8_t rom[192];
	struct GXAPack* pack = openFixture("fixture.gxa");
	struct GXAPack* bySha = openFixture("sha1.gxa");
	CHECK(pack && bySha);
	if (!pack || !bySha) {
		return;
	}
	testRom(rom, "GXAT");
	CHECK(GXAPackMatch(pack, rom, sizeof(rom)) == GXA_MATCH_HEADER);
	CHECK(GXAPackMatch(bySha, rom, sizeof(rom)) == GXA_MATCH_SHA1);
	testRom(rom, "GXAU");
	CHECK(GXAPackMatch(pack, rom, sizeof(rom)) == GXA_MATCH_NONE);
	CHECK(GXAPackMatch(bySha, rom, sizeof(rom)) == GXA_MATCH_NONE);
	uint8_t a[20], b[20];
	GXAPackId(pack, a);
	GXAPackId(bySha, b);
	CHECK(memcmp(a, b, 20));
	GXAPackClose(pack);
	GXAPackClose(bySha);
}

// a header rule with a version range (ROM header 0xBC): in range is a full match, out of range only a header match
static void gxa_pack_version(void) {
	uint8_t rom[192];
	struct GXAPack* pack = openFixture("version.gxa");
	CHECK(pack);
	if (!pack) {
		return;
	}
	testRom(rom, "GXAT");
	const uint8_t want[][2] = { { 1, GXA_MATCH_HEADER }, { 2, GXA_MATCH_VERSION }, { 5, GXA_MATCH_VERSION }, { 6, GXA_MATCH_HEADER } };
	size_t i;
	for (i = 0; i < sizeof(want) / sizeof(*want); ++i) {
		rom[0xBC] = want[i][0];
		CHECK(GXAPackMatch(pack, rom, sizeof(rom)) == want[i][1]);
	}
	testRom(rom, "GXAV");   // a bare number: that version and later
	rom[0xBC] = 2;
	CHECK(GXAPackMatch(pack, rom, sizeof(rom)) == GXA_MATCH_HEADER);
	rom[0xBC] = 255;
	CHECK(GXAPackMatch(pack, rom, sizeof(rom)) == GXA_MATCH_VERSION);
	GXAPackClose(pack);
}

int main(void) {
	gxa_pack_tracks();
	gxa_pack_version();
	gxa_pack_rejects();
	gxa_pack_match();
	printf("gxa-pack: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
