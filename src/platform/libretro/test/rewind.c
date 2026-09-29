/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The rewind ring: one snapshot per 60 frames, a step lands on the snapshot
// that many seconds back and drops it with every newer one, so repeated steps
// keep walking back until the ring is empty.

#include "../link.h"
#include "../rewind.h"
#include "link-rom.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t saves[2][0x20000];
static mColor video[256 * 224];

static void run(struct mCore* core, struct RetroRewind* rewind, unsigned frames) {
	unsigned i;
	for (i = 0; i < frames; ++i) {
		core->runFrame(core);
		RetroRewindFrame(rewind, core);
	}
}

int main(void) {
	static uint8_t rom[LINK_GBA_ROM_SIZE];
	linkBuildGbaRom(rom);
	struct RetroLinkCart carts[2];
	unsigned i;
	for (i = 0; i < 2; ++i) {
		memset(saves[i], 0xFF, sizeof(saves[i]));
		carts[i] = (struct RetroLinkCart) { rom, sizeof(rom), saves[i], sizeof(saves[i]) };
	}
	// The link harness is the tested way to get a loaded core; ending it leaves
	// a single-player one.
	struct mCore* core = RetroLinkEnd(RetroLinkCreate(mPLATFORM_GBA, carts, 2, 0, 1700000000000LL, video, 256));

	struct RetroRewind rewind = {0};
	run(core, &rewind, 120);
	CHECK(RetroRewindStep(&rewind, core, 1) == 0, "an unconfigured ring kept a snapshot");

	RetroRewindConfigure(&rewind, 4);
	uint32_t start = core->frameCounter(core);
	run(core, &rewind, 60 * 6); // 6 snapshots into 4 slots: the first 2 are gone
	CHECK(RetroRewindStep(&rewind, core, 2) == 2, "step 2 of 4 kept did not rewind 2");
	CHECK(core->frameCounter(core) == start + 60 * 5, "step 2 landed on frame %u, want %u",
	      core->frameCounter(core) - start, 60 * 5);
	CHECK(RetroRewindStep(&rewind, core, 5) == 2, "a step past the oldest did not stop at it");
	CHECK(core->frameCounter(core) == start + 60 * 3, "the oldest kept is frame %u, want %u",
	      core->frameCounter(core) - start, 60 * 3);
	CHECK(RetroRewindStep(&rewind, core, 1) == 0, "an emptied ring still rewound");

	// Capturing resumes from the restored point.
	run(core, &rewind, 60);
	CHECK(RetroRewindStep(&rewind, core, 1) == 1, "no snapshot after rewinding and playing on");
	CHECK(core->frameCounter(core) == start + 60 * 4, "resumed capture landed on frame %u, want %u",
	      core->frameCounter(core) - start, 60 * 4);

	run(core, &rewind, 120);
	RetroRewindClear(&rewind);
	CHECK(RetroRewindStep(&rewind, core, 1) == 0, "clear kept a snapshot");

	RetroRewindConfigure(&rewind, 0);
	run(core, &rewind, 120);
	CHECK(RetroRewindStep(&rewind, core, 1) == 0, "a ring configured to 0 kept a snapshot");

	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
