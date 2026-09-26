/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The HLE BgAffineSet / ObjAffineSet must match the real BIOS bit for bit.
// Their results are written into game memory, so any difference is a
// difference in emulated state -- and when it comes from sinf/cosf, whose
// last bit varies between Apple libm and bionic, iOS and Android desync in
// linked play. The golden table was produced from a real BIOS by
// bios-affine-emit.c; integer code reproduces it on any compiler.

#include "affine-rom.h"
#include "bios-affine-golden.h"

#include <stdio.h>

int main(void) {
	static uint32_t rom[AFFINE_ROM_WORDS];
	static uint16_t out[AFFINE_OUT_HALFWORDS];
	int failures = 0;
	affineBuildRom(rom);

	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->reset(core);
	if (!affineRun(core, rom, out)) {
		printf("FAIL: affine ROM did not finish on the HLE BIOS\n");
		++failures;
	} else {
		int i, mismatches = 0;
		for (i = 0; i < AFFINE_OUT_HALFWORDS; ++i) {
			if (out[i] != affineGolden[i]) {
				if (!mismatches) {
					printf("FAIL: halfword %d (%s case %d): HLE %04X, BIOS %04X\n", i,
					       i < AFFINE_BG_COUNT * 8 ? "Bg" : "Obj",
					       i < AFFINE_BG_COUNT * 8 ? i / 8 : (i - AFFINE_BG_COUNT * 8) / 4,
					       out[i], affineGolden[i]);
				}
				++mismatches;
			}
		}
		if (mismatches) {
			printf("FAIL: %d of %d halfwords differ from the real BIOS\n", mismatches, AFFINE_OUT_HALFWORDS);
			++failures;
		}
	}
	mCoreConfigDeinit(&core->config);
	core->deinit(core);

	printf("1 checks, %d failures\n", failures);
	return failures != 0;
}
