/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A LAN peer may still run the core that shipped before this one:
// libretro-build/link_compat.txt lets this core answer with that core's id,
// and these checksums, recorded on it, hold the claim. When this fails the
// link code changed what is emulated: drop the link_compat.txt entry.
//
// Recording: build this file against the shipped core's sources with
// -DLINK_COMPAT_EMIT and save its output as link-compat-golden.h.

#include "../link.h"
#include "link-rom.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef LINK_COMPAT_EMIT
#include "link-compat-golden.h"
#endif

enum { RUNS = 3, SAMPLES = 10 };

static uint8_t saves[2][0x20000];
static mColor video[256 * 224];

static void run(enum mPlatform platform, const uint8_t* rom, size_t size, uint32_t out[SAMPLES]) {
	struct RetroLinkCart carts[2];
	unsigned i;
	for (i = 0; i < 2; ++i) {
		memset(saves[i], 0xFF, sizeof(saves[i]));
		carts[i] = (struct RetroLinkCart) { rom, size, saves[i], sizeof(saves[i]) };
	}
	struct RetroLink* link = RetroLinkCreate(platform, carts, 2, 0, 1700000000000LL, video, 256);
	if (!link) {
		printf("FAIL: RetroLinkCreate refused the test ROM\n");
		exit(1);
	}
	unsigned frame;
	for (frame = 0; frame < SAMPLES * 60; ++frame) {
		uint16_t masks[2] = {
			(uint16_t) (((frame * 2654435761u) >> 13) & 0x0FFF),
			(uint16_t) (((frame * 2654435761u) >> 7) & 0x0FFF),
		};
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
		if (frame % 60 == 59) {
			out[frame / 60] = RetroLinkChecksum(link);
		}
	}
	struct mCore* local = RetroLinkEnd(link);
	mCoreConfigDeinit(&local->config);
	local->deinit(local);
}

int main(void) {
	static uint8_t gba[LINK_GBA_ROM_SIZE];
	static uint8_t gb[LINK_GB_ROM_SIZE];
	static uint8_t cgb[LINK_GB_ROM_SIZE];
	linkBuildGbaRom(gba);
	linkBuildGbRom(gb, false);
	linkBuildGbRom(cgb, true);
	uint32_t sums[RUNS][SAMPLES];
	run(mPLATFORM_GBA, gba, sizeof(gba), sums[0]);
	run(mPLATFORM_GB, gb, sizeof(gb), sums[1]);
	run(mPLATFORM_GB, cgb, sizeof(cgb), sums[2]);

	unsigned r, i;
#ifdef LINK_COMPAT_EMIT
	printf("// Recorded on the shipped core by link-compat.c; see there.\n");
	printf("static const uint32_t linkCompatGolden[%d][%d] = {\n", RUNS, SAMPLES);
	for (r = 0; r < RUNS; ++r) {
		printf("\t{");
		for (i = 0; i < SAMPLES; ++i) {
			printf(" 0x%08X,", sums[r][i]);
		}
		printf(" },\n");
	}
	printf("};\n");
	return 0;
#else
	static const char* names[RUNS] = { "GBA", "GB", "GBC double speed" };
	int failures = 0;
	for (r = 0; r < RUNS; ++r) {
		if (memcmp(sums[r], linkCompatGolden[r], sizeof(sums[r]))) {
			++failures;
			printf("FAIL: %s link emulation differs from the shipped core\n", names[r]);
		}
	}
	printf("%d checks, %d failures\n", RUNS, failures);
	return failures != 0;
#endif
}
