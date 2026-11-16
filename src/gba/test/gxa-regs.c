/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GXA's registers through the GBA bus: the handshake only answers with GXA
// attached, a play without its track (or without a pack) ends at once, and a
// 32-bit store sets TRACK before CTRL acts.

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/gxa.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond); ++failures; } } while (0)

#define REG(off) (0x04FFFA00 + (off))
#define CH(n, off) REG(0x10 + 0x10 * (n) + (off))
enum { TRACK = 0, CTRL = 2, VOL = 4, FADE = 6, STATE = 8, POS = 0xC };
enum { S_PLAYING = 1, S_PAUSED = 2, S_ENDED = 4, S_MISSING = 8, S_ERROR = 16 };

static uint8_t rom[0x400];

static struct GXAPack* fixture(void) {
	char path[1024];
	const char* slash = strrchr(__FILE__, '/');
	snprintf(path, sizeof(path), "%.*s/gxa/fixture.gxa", (int) (slash - __FILE__), __FILE__);
	return GXAPackOpen(VFileOpen(path, O_RDONLY));
}

static struct mCore* load(bool gxa, bool pack) {
	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->reset(core);
	if (gxa) {
		struct GXA* g = GXACreate();
		GXASetPack(g, pack ? fixture() : NULL);
		((struct GBA*) core->board)->gxa = g;
	}
	return core;
}

static void unload(struct mCore* core) {
	struct GBA* gba = core->board;
	if (gba->gxa) {
		GXADestroy(gba->gxa);
		gba->gxa = NULL;
	}
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

static void gxa_regs_absent(void) {
	struct mCore* core = load(false, false);
	core->busWrite16(core, REG(4), 0x1234);
	CHECK(core->busRead16(core, REG(4)) != (0x1234 ^ 0x5847));
	unload(core);
}

static void gxa_regs_handshake(void) {
	struct mCore* core = load(true, true);
	CHECK(core->busRead16(core, REG(0)) == 0x5847);
	CHECK(core->busRead16(core, REG(2)) == 0x3141);
	core->busWrite16(core, REG(4), 0x1234);
	CHECK(core->busRead16(core, REG(4)) == (0x1234 ^ 0x5847));
	CHECK(core->busRead16(core, REG(6)) == 0x0101);
	CHECK(core->busRead16(core, REG(8)) == 0x0401);
	CHECK(core->busRead32(core, REG(0)) == 0x31415847);
	unload(core);
}

// PACKVER: the manifest's "version" ("1" in the fixture), padded with 0; all 0 without a pack
static void gxa_regs_packver(void) {
	struct mCore* core = load(true, true);
	CHECK(core->busRead16(core, REG(0x50)) == '1');
	CHECK(core->busRead16(core, REG(0x5E)) == 0);
	unload(core);
	core = load(true, false);
	CHECK(core->busRead16(core, REG(0x50)) == 0);
	unload(core);
}

static void gxa_regs_play(void) {
	struct mCore* core = load(true, true);
	core->busWrite16(core, CH(0, TRACK), 1);
	core->busWrite16(core, CH(0, CTRL), 1);
	CHECK(core->busRead16(core, CH(0, STATE)) == S_PLAYING);
	core->busWrite16(core, CH(0, CTRL), 4);
	CHECK(core->busRead16(core, CH(0, STATE)) == S_PAUSED);
	core->busWrite16(core, CH(0, CTRL), 5);
	CHECK(core->busRead16(core, CH(0, STATE)) == S_PLAYING);
	core->busWrite16(core, CH(0, CTRL), 3);
	CHECK(core->busRead16(core, CH(0, STATE)) == 0);
	CHECK(core->busRead16(core, CH(1, STATE)) == 0);
	unload(core);
}

static void gxa_regs_missing(void) {
	struct mCore* core = load(true, true);
	core->busWrite16(core, CH(2, TRACK), 7);
	core->busWrite16(core, CH(2, CTRL), 1);
	CHECK(core->busRead16(core, CH(2, STATE)) == (S_MISSING | S_ENDED));
	unload(core);
}

static void gxa_regs_no_pack(void) {
	struct mCore* core = load(true, false);
	CHECK(core->busRead16(core, REG(8)) == 0x0400);
	core->busWrite16(core, CH(0, TRACK), 1);
	core->busWrite16(core, CH(0, CTRL), 2);
	CHECK(core->busRead16(core, CH(0, STATE)) == (S_MISSING | S_ENDED));
	unload(core);
}

static void gxa_regs_store32(void) {
	struct mCore* core = load(true, true);
	core->busWrite32(core, CH(3, TRACK), (1 << 16) | 1);
	CHECK(core->busRead16(core, CH(3, STATE)) == S_PLAYING);
	unload(core);
}

static void gxa_regs_gain(void) {
	struct mCore* core = load(true, true);
	struct GXA* gxa = ((struct GBA*) core->board)->gxa;
	CHECK(GXAGameGain(gxa) == 256);
	core->busWrite16(core, REG(0xA), 128);
	core->busWrite16(core, REG(0xC), 64);
	CHECK(core->busRead16(core, REG(0xA)) == 128 && core->busRead16(core, REG(0xC)) == 64);
	CHECK(GXAGameGain(gxa) == 128);
	core->busWrite16(core, CH(0, TRACK), 1);
	core->busWrite16(core, CH(0, CTRL), 1);
	CHECK(GXAGameGain(gxa) == 32);
	unload(core);
}

// 0x60-0x7F is reserved and PACKVER (0x50-0x5F) read-only: writes there change nothing
static void gxa_regs_reserved(void) {
	struct mCore* core = load(true, true);
	core->busWrite16(core, CH(0, TRACK), 2);
	core->busWrite16(core, CH(0, CTRL), 2);
	struct GXA* gxa = ((struct GBA*) core->board)->gxa;
	GXAProduce(gxa, 4000, 32768);
	uint32_t off;
	for (off = 0x50; off < 0x80; off += 2) {
		core->busWrite16(core, REG(off), 3);
	}
	CHECK(core->busRead16(core, REG(0x50)) == '1');   // PACKVER is read-only
	for (off = 0x60; off < 0x80; off += 2) {
		CHECK(core->busRead16(core, REG(off)) == 0);
	}
	CHECK(core->busRead16(core, CH(0, STATE)) == S_PLAYING);
	unload(core);
}

int main(void) {
	memcpy(&rom[0xA0], "GXA TEST", 8);
	memcpy(&rom[0xAC], "GXAT", 4);
	gxa_regs_absent();
	gxa_regs_handshake();
	gxa_regs_packver();
	gxa_regs_play();
	gxa_regs_missing();
	gxa_regs_no_pack();
	gxa_regs_store32();
	gxa_regs_gain();
	gxa_regs_reserved();
	printf("gxa-regs: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
