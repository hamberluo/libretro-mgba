/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GBA link mode: the cable carries data, each player's input reaches its
// own core, the result is deterministic, and a game that never uses the
// cable still runs one frame per step.

#include "../link.h"
#include "link-rom.h"

#include <mgba/core/interface.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>

#include <stdio.h>
#include <stdlib.h>

#define JOYPAD_B (1 << 0)
#define JOYPAD_A (1 << 8)
#define FRAME_TICKS 280896

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t rom[LINK_GBA_ROM_SIZE];
static uint8_t saves[RETRO_LINK_MAX_PLAYERS][0x20000];
static mColor localVideo[256 * 224];

static struct RetroLink* makeLink(const uint8_t* image, unsigned local) {
	struct RetroLinkSave s[RETRO_LINK_MAX_PLAYERS];
	unsigned i;
	for (i = 0; i < RETRO_LINK_MAX_PLAYERS; ++i) {
		memset(saves[i], 0xFF, sizeof(saves[i]));
		s[i].data = saves[i];
		s[i].size = sizeof(saves[i]);
	}
	memset(localVideo, 0, sizeof(localVideo));
	return RetroLinkCreate(mPLATFORM_GBA, image, LINK_GBA_ROM_SIZE, 2, local, s, 1700000000000LL, localVideo, 256);
}

static void endAndFree(struct RetroLink* link) {
	struct mCore* local = RetroLinkEnd(link);
	mCoreConfigDeinit(&local->config);
	local->deinit(local);
}

static uint16_t input(unsigned frame, unsigned player) {
	// A fixed, non-trivial pattern: each player presses something different.
	return (uint16_t) (((frame * 2654435761u) >> (player ? 7 : 13)) & 0x0FFF);
}

static void checksums(uint32_t out[10]) {
	struct RetroLink* link = makeLink(rom, 0);
	unsigned frame;
	for (frame = 0; frame < 600; ++frame) {
		uint16_t masks[2] = { input(frame, 0), input(frame, 1) };
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

	// Traffic and input routing.
	struct RetroLink* link = makeLink(rom, 1);
	CHECK(link, "RetroLinkCreate refused a valid GBA ROM");
	CHECK(RetroLinkLocalCore(link) == RetroLinkCore(link, 1), "local core is not player 1's");
	struct mCore* p1 = RetroLinkCore(link, 0);
	struct mCore* p2 = RetroLinkCore(link, 1);
	CHECK(!((struct GBA*) p1->board)->memory.fullBios, "link mode loaded a BIOS");
	CHECK(p1->rtc.override == RTC_FAKE_EPOCH && p1->rtc.value == 1700000000000LL, "RTC is not the shared fake epoch");

	uint16_t masks[2] = { 0, JOYPAD_A };
	int f;
	for (f = 0; f < 5; ++f) {
		RetroLinkSetInput(link, masks);
		CHECK(RetroLinkRunFrame(link), "frame %d: every core blocked", f);
	}
	CHECK(p1->busRead16(p1, LINK_GBA_WORD1) == 0x03FE, "P2's A did not reach P1 (SIOMULTI1 %04X)", p1->busRead16(p1, LINK_GBA_WORD1));
	masks[0] = JOYPAD_B;
	masks[1] = 0;
	for (f = 0; f < 5; ++f) {
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
	}
	CHECK(p2->busRead16(p2, LINK_GBA_WORD0) == 0x03FD, "P1's B did not reach P2 (SIOMULTI0 %04X)", p2->busRead16(p2, LINK_GBA_WORD0));
	CHECK(p1->busRead32(p1, LINK_GBA_COUNT) > 300, "only %u transfers in 10 frames", p1->busRead32(p1, LINK_GBA_COUNT));
	int drawn = 0;
	size_t px;
	for (px = 0; px < 256 * 160; ++px) {
		drawn += localVideo[px] == LINK_GBA_BACKDROP;
	}
	CHECK(drawn > 0, "the local core drew nothing into its video buffer");
	CHECK(p1->frameCounter(p1) == 10 && p2->frameCounter(p2) == 10, "10 steps ran %u / %u frames", p1->frameCounter(p1), p2->frameCounter(p2));
	endAndFree(link);

	// Determinism, and the checksum sees input.
	uint32_t first[10], second[10];
	checksums(first);
	checksums(second);
	CHECK(memcmp(first, second, sizeof(first)) == 0, "same input, different checksums");
	CHECK(first[0] != first[9], "checksum did not change over 600 frames");

	// A game that never touches the cable.
	static uint8_t idle[LINK_GBA_ROM_SIZE];
	linkBuildGbaIdleRom(idle);
	link = makeLink(idle, 0);
	struct mCore* a = RetroLinkCore(link, 0);
	struct mCore* b = RetroLinkCore(link, 1);
	int32_t start = mTimingCurrentTime(&((struct GBA*) a->board)->timing);
	for (f = 0; f < 300; ++f) {
		masks[0] = masks[1] = 0;
		RetroLinkSetInput(link, masks);
		CHECK(RetroLinkRunFrame(link), "idle game: every core blocked at frame %d", f);
	}
	int32_t ran = mTimingCurrentTime(&((struct GBA*) a->board)->timing) - start;
	CHECK(ran >= 300 * FRAME_TICKS && ran < 302 * FRAME_TICKS, "idle game: 300 steps ran %d ticks", ran);
	CHECK(b->frameCounter(b) == 300, "idle game: remote ran %u frames", b->frameCounter(b));
	endAndFree(link);

	// Unplug mid-transfer: the local core must keep running on its own, not
	// stay parked by a coordinator that is gone.
	link = makeLink(rom, 1);
	for (f = 0; f < 3; ++f) {
		masks[0] = masks[1] = 0;
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
	}
	struct mCore* alone = RetroLinkEnd(link);
	uint32_t before = alone->frameCounter(alone);
	for (f = 0; f < 60; ++f) {
		alone->runFrame(alone);
	}
	CHECK(alone->frameCounter(alone) == before + 60, "after unplugging, 60 runFrame calls ran %u frames",
	      alone->frameCounter(alone) - before);
	CHECK(!((struct GBA*) alone->board)->sio.driver, "local core still has the lockstep driver");
	// The link's neutral sensors die with it; the local core must not keep them.
	CHECK(!((struct GBA*) alone->board)->rotationSource && !((struct GBA*) alone->board)->luminanceSource,
	      "local core still points at the freed link's sensors");
	mCoreConfigDeinit(&alone->config);
	alone->deinit(alone);

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
