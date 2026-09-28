/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Same-device play shows one player at a time: the viewed core draws into the
// frontend's buffer and keeps its audio, and ending the link from a remote
// view hands the local core its buffer back (run under ASan: the remote's
// scratch buffer is freed by then).

#include "../link.h"
#include "link-rom.h"

#include <mgba/core/interface.h>
#include <mgba-util/audio-buffer.h>

#include <stdio.h>
#include <stdlib.h>

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

// BGR555, neither white nor black.
static const uint16_t colours[4] = { 0x03E0, 0x7C00, 0x001F, 0x7C1F };
static uint8_t roms[4][LINK_GBA_ROM_SIZE];
static uint8_t saves[4][0x20000];
static mColor localVideo[256 * 224];

static int shown(uint16_t colour) {
	int n = 0;
	size_t px;
	for (px = 0; px < 256 * 160; ++px) {
		n += localVideo[px] == colour;
	}
	return n;
}

static void run(struct RetroLink* link, int frames) {
	uint16_t masks[4] = { 0 };
	int f;
	for (f = 0; f < frames; ++f) {
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
	}
}

int main(void) {
	struct RetroLinkCart c[4];
	unsigned i;
	for (i = 0; i < 4; ++i) {
		linkBuildGbaRomWithBackdrop(roms[i], colours[i]);
		memset(saves[i], 0xFF, sizeof(saves[i]));
		c[i] = (struct RetroLinkCart) { roms[i], LINK_GBA_ROM_SIZE, saves[i], sizeof(saves[i]) };
	}
	struct RetroLink* link = RetroLinkCreate(mPLATFORM_GBA, c, 4, 0, 0, localVideo, 256);
	CHECK(link, "RetroLinkCreate refused four GBAs");
	if (!link) {
		printf("%d checks, %d failures\n", checks, failures);
		return 1;
	}
	run(link, 3);
	CHECK(RetroLinkViewCore(link) == RetroLinkCore(link, 0), "the local player is not viewed at start");
	CHECK(shown(colours[0]) > 0, "P1 is not on screen at start");

	CHECK(RetroLinkSetView(link, 2), "viewing P3 was refused");
	memset(localVideo, 0, sizeof(localVideo));
	run(link, 2);
	CHECK(RetroLinkViewCore(link) == RetroLinkCore(link, 2), "P3 is not the viewed core");
	CHECK(shown(colours[2]) > 0 && shown(colours[0]) == 0, "P3 not on screen after switching (P3 %d, P1 %d)",
	      shown(colours[2]), shown(colours[0]));
	struct mCore* p1 = RetroLinkCore(link, 0);
	struct mCore* p3 = RetroLinkCore(link, 2);
	CHECK(mAudioBufferAvailable(p3->getAudioBuffer(p3)) > 0, "the viewed core's audio was discarded");
	CHECK(mAudioBufferAvailable(p1->getAudioBuffer(p1)) == 0, "the local core's audio was kept while not viewed");

	CHECK(!RetroLinkSetView(link, 4), "an out-of-range player was accepted");
	CHECK(RetroLinkViewCore(link) == p3, "a refused view changed the viewed core");

	CHECK(RetroLinkSetView(link, 0), "viewing P1 again was refused");
	memset(localVideo, 0, sizeof(localVideo));
	run(link, 2);
	CHECK(shown(colours[0]) > 0, "P1 did not draw again after switching back");

	// Rapid taps: a new view every frame, ending on P2.
	int tap;
	for (tap = 0; tap < 22; ++tap) {
		RetroLinkSetView(link, tap % 4);
		run(link, 1);
	}
	memset(localVideo, 0, sizeof(localVideo));
	run(link, 1);
	CHECK(shown(colours[1]) > 0, "after switching every frame, P2 (the last chosen) is not on screen");

	// End while a remote is viewed: the local core must draw into localVideo.
	RetroLinkSetView(link, 3);
	run(link, 2);
	struct mCore* alone = RetroLinkEnd(link);
	memset(localVideo, 0, sizeof(localVideo));
	int f;
	for (f = 0; f < 2; ++f) {
		alone->runFrame(alone);
	}
	CHECK(shown(colours[0]) > 0, "after ending from P4's view, P1 draws nothing into the frontend buffer");
	mCoreConfigDeinit(&alone->config);
	alone->deinit(alone);

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
