/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GXA's state rides in the GBA core's extra state (extdata): a load resumes the same
// samples, a changed pack ends what played, a state without GXA resets it,
// and the libretro rewind ring carries it.

#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/gxa.h>
#include <mgba-util/vfs.h>

#include "platform/libretro/rewind.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond); ++failures; } } while (0)

#define REG(off) (0x04FFFA00 + (off))
#define CH0(off) REG(0x10 + (off))
enum { TRACK = 0, CTRL = 2, STATE = 8, POS = 0xC };
enum { S_PLAYING = 1, S_ENDED = 4 };

static uint8_t rom[0x80000];

static struct mCore* load(void) {
	char path[1024];
	const char* slash = strrchr(__FILE__, '/');
	snprintf(path, sizeof(path), "%.*s/gxa/fixture.gxa", (int) (slash - __FILE__), __FILE__);
	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->reset(core);
	struct GXA* gxa = GXACreate();
	GXASetPack(gxa, GXAPackOpen(VFileOpen(path, O_RDONLY)));
	((struct GBA*) core->board)->gxa = gxa;
	return core;
}

static struct GXA* gxaOf(struct mCore* core) {
	return ((struct GBA*) core->board)->gxa;
}

static void unload(struct mCore* core) {
	GXADestroy(gxaOf(core));
	((struct GBA*) core->board)->gxa = NULL;
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

static int16_t buf[0x10000 * 2];

// Runs frames; returns the GXA frames rendered meanwhile (left channel in out).
static size_t run(struct mCore* core, int frames, int16_t* out) {
	size_t n = 0;
	int i;
	for (i = 0; i < frames; ++i) {
		core->runFrame(core);
		size_t got;
		int16_t tmp[2048 * 2];
		while ((got = GXARead(gxaOf(core), tmp, 2048)) > 0) {
			size_t j;
			for (j = 0; j < got; ++j) {
				if (out && n < 0x10000) {
					out[n] = tmp[j * 2];
				}
				++n;
			}
		}
	}
	return n;
}

static void play(struct mCore* core, int track) {
	core->busWrite16(core, CH0(TRACK), track);
	core->busWrite16(core, CH0(CTRL), 1);
}

static uint32_t pos(struct mCore* core) {
	return core->busRead16(core, CH0(POS)) | core->busRead16(core, CH0(POS + 2)) << 16;
}

static void gxa_state_roundtrip(void) {
	struct mCore* core = load();
	play(core, 1);
	run(core, 18, NULL);   // ~0.3 s
	uint32_t saved = pos(core);
	struct VFile* vf = VFileMemChunk(NULL, 0);
	CHECK(mCoreSaveStateNamed(core, vf, 0));
	static int16_t a[0x10000], b[0x10000];
	size_t na = run(core, 12, a);
	vf->seek(vf, 0, SEEK_SET);
	CHECK(mCoreLoadStateNamed(core, vf, 0));
	CHECK(pos(core) == saved);
	CHECK(core->busRead16(core, CH0(STATE)) == S_PLAYING);
	size_t nb = run(core, 12, b);
	CHECK(na == nb && na > 0);
	size_t i;
	int maxDiff = 0;
	for (i = 0; i < na && i < nb; ++i) {
		int d = abs(a[i] - b[i]);
		maxDiff = d > maxDiff ? d : maxDiff;
	}
	CHECK(maxDiff <= 1);
	vf->close(vf);
	unload(core);
}

static void gxa_state_pack_changed(void) {
	struct mCore* core = load();
	play(core, 1);
	run(core, 6, NULL);
	struct VFile* vf = VFileMemChunk(NULL, 0);
	CHECK(mCoreSaveStateNamed(core, vf, 0));
	GXASetPack(gxaOf(core), NULL);
	vf->seek(vf, 0, SEEK_SET);
	CHECK(mCoreLoadStateNamed(core, vf, 0));
	CHECK(core->busRead16(core, CH0(STATE)) == S_ENDED);
	vf->close(vf);
	unload(core);
}

static void gxa_state_without_extdata(void) {
	struct mCore* core = load();
	core->busWrite16(core, REG(0xA), 100);
	play(core, 1);
	run(core, 2, NULL);
	// what a state saved before GXA existed carries: no GXA item
	struct mStateExtdata old;
	mStateExtdataInit(&old);
	core->loadExtraState(core, &old);
	mStateExtdataDeinit(&old);
	CHECK(core->busRead16(core, CH0(STATE)) == 0);
	CHECK(core->busRead16(core, REG(0xA)) == 256);
	unload(core);
}

static void gxa_state_rewind(void) {
	struct mCore* core = load();
	struct RetroRewind ring = { 0 };
	RetroRewindConfigure(&ring, 10);
	int i;
	for (i = 0; i < 70; ++i) {
		run(core, 1, NULL);
		RetroRewindFrame(&ring, core);
	}
	play(core, 1);
	for (i = 0; i < 20; ++i) {
		run(core, 1, NULL);
		RetroRewindFrame(&ring, core);
	}
	CHECK(core->busRead16(core, CH0(STATE)) == S_PLAYING);
	CHECK(RetroRewindStep(&ring, core, 1) == 1);
	CHECK(core->busRead16(core, CH0(STATE)) == 0);
	RetroRewindConfigure(&ring, 0);
	unload(core);
}

int main(void) {
	memcpy(rom, "\xFE\xFF\xFF\xEA", 4);
	memcpy(&rom[0xA0], "GXA TEST", 8);
	memcpy(&rom[0xAC], "GXAT", 4);
	rom[0xB2] = 0x96;
	gxa_state_roundtrip();
	gxa_state_pack_changed();
	gxa_state_without_extdata();
	gxa_state_rewind();
	printf("gxa-state: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
