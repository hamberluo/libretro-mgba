/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A ROM whose header logo was damaged by a patch must still boot through a
// real BIOS: the core restores the logo in memory rather than skipping the
// boot animation, since the BIOS itself hangs on a logo it does not know.

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/crc32.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <string.h>

int main(void) {
	static uint8_t rom[0x200];
	static uint32_t bios[GBA_SIZE_BIOS / 4];
	int failures = 0;

	// Seven vectors of branches is all GBAIsBIOS asks of a BIOS image.
	int i;
	for (i = 0; i < 7; ++i) {
		bios[i] = 0xEA000000;
	}
	// A zeroed logo, as far from the real one as a patch can leave it.
	memset(rom, 0, sizeof(rom));

	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->loadBIOS(core, VFileFromConstMemory(bios, sizeof(bios)), 0);
	core->reset(core);

	struct GBA* gba = core->board;
	uint32_t pc = gba->cpu->gprs[ARM_PC];
	if (pc >> 24) {
		printf("FAIL: BIOS skipped, pc=%08X\n", pc);
		++failures;
	}
	if (doCrc32(&gba->memory.rom[1], 0x9C) != 0xD0BEB55E) {
		printf("FAIL: logo not restored for the BIOS\n");
		++failures;
	}
	mCoreConfigDeinit(&core->config);
	core->deinit(core);

	printf("2 checks, %d failures\n", failures);
	return failures != 0;
}
