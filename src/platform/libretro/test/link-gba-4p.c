/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Four GBAs on one cable, as one device runs them for same-device play and
// every device will for LAN 4P: each player's word reaches the parent, all
// four advance one frame per step, and the run is deterministic.

#include "../link.h"
#include "link-rom.h"

#include <mgba/core/interface.h>
#include <mgba/internal/gba/gba.h>

#include <stdio.h>
#include <stdlib.h>

#define JOYPAD_B (1 << 0)
#define JOYPAD_A (1 << 8)
#define JOYPAD_SELECT (1 << 2)

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t rom[LINK_GBA_ROM_SIZE];
static uint8_t saves[4][0x20000];
static mColor localVideo[256 * 224];

static struct RetroLink* makeLink(unsigned players) {
	struct RetroLinkCart c[4];
	unsigned i;
	for (i = 0; i < players; ++i) {
		memset(saves[i], 0xFF, sizeof(saves[i]));
		c[i] = (struct RetroLinkCart) { rom, LINK_GBA_ROM_SIZE, saves[i], sizeof(saves[i]) };
	}
	return RetroLinkCreate(mPLATFORM_GBA, c, players, 0, 1700000000000LL, localVideo, 256);
}

static void endAndFree(struct RetroLink* link) {
	struct mCore* local = RetroLinkEnd(link);
	mCoreConfigDeinit(&local->config);
	local->deinit(local);
}

static void checksums(uint32_t out[10]) {
	struct RetroLink* link = makeLink(4);
	unsigned frame;
	for (frame = 0; frame < 600; ++frame) {
		uint16_t masks[4];
		unsigned p;
		for (p = 0; p < 4; ++p) {
			masks[p] = (uint16_t) (((frame * 2654435761u) >> (5 + 3 * p)) & 0x0FFF);
		}
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
		if (frame % 60 == 59) {
			out[frame / 60] = RetroLinkChecksum(link);
		}
	}
	endAndFree(link);
}

int main(void) {
	linkBuildGbaRom(rom);

	struct RetroLink* link = makeLink(4);
	CHECK(link, "RetroLinkCreate refused four GBAs");
	if (!link) {
		printf("%d checks, %d failures\n", checks, failures);
		return 1;
	}
	// P4 presses nothing: an idle player must not hold the others back.
	uint16_t masks[4] = { 0, JOYPAD_A, JOYPAD_B, 0 };
	int f;
	for (f = 0; f < 10; ++f) {
		RetroLinkSetInput(link, masks);
		CHECK(RetroLinkRunFrame(link), "frame %d: every core blocked", f);
	}
	struct mCore* p1 = RetroLinkCore(link, 0);
	CHECK(p1->busRead16(p1, LINK_GBA_WORD1) == 0x03FE, "P2's A did not reach P1 (%04X)", p1->busRead16(p1, LINK_GBA_WORD1));
	CHECK(p1->busRead16(p1, LINK_GBA_WORD2) == 0x03FD, "P3's B did not reach P1 (%04X)", p1->busRead16(p1, LINK_GBA_WORD2));
	CHECK(p1->busRead16(p1, LINK_GBA_WORD3) == 0x03FF, "P4's idle word did not reach P1 (%04X)", p1->busRead16(p1, LINK_GBA_WORD3));
	unsigned p;
	for (p = 0; p < 4; ++p) {
		struct mCore* c = RetroLinkCore(link, p);
		CHECK(c->frameCounter(c) == 10, "P%u ran %u frames in 10 steps", p + 1, c->frameCounter(c));
	}
	CHECK(p1->busRead32(p1, LINK_GBA_COUNT) > 150, "only %u transfers in 10 frames", p1->busRead32(p1, LINK_GBA_COUNT));
	endAndFree(link);

	// Three players is a valid cable too.
	link = makeLink(3);
	CHECK(link, "RetroLinkCreate refused three GBAs");
	if (link) {
		masks[0] = masks[1] = masks[2] = 0;
		for (f = 0; f < 5; ++f) {
			RetroLinkSetInput(link, masks);
			CHECK(RetroLinkRunFrame(link), "3P frame %d: every core blocked", f);
		}
		endAndFree(link);
	}

	uint32_t first[10], second[10];
	checksums(first);
	checksums(second);
	CHECK(memcmp(first, second, sizeof(first)) == 0, "four players, same input, different checksums");

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
