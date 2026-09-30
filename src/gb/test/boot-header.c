/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A ROM whose header logo or checksum was damaged by a patch must still pass
// the boot ROM, which otherwise hangs. The fix lives only in the boot-time
// copy of bank 0, so the game keeps reading its own header afterwards.

#include <mgba/core/core.h>
#include <mgba/gb/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba-util/crc32.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <string.h>

int main(void) {
	static uint8_t rom[0x8000];
	static uint8_t bios[0x100];
	int failures = 0;

	// The first four logo bytes are what GBIsROM asks for; the rest, and the
	// header checksum, are left wrong.
	static const uint8_t logoStart[4] = { 0xCE, 0xED, 0x66, 0x66 };
	memcpy(&rom[0x104], logoStart, sizeof(logoStart));
	rom[0x14D] = 0x5A;

	struct mCore* core = GBCoreCreate();
	core->init(core);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	struct GB* gb = core->board;
	gb->biosVf = VFileFromConstMemory(bios, sizeof(bios));
	GBMapBIOS(gb);

	const uint8_t* booted = gb->memory.romBase;
	if (doCrc32(&booted[0x104], 0x30) != 0x46195417) {
		printf("FAIL: boot ROM sees a bad logo\n");
		++failures;
	}
	uint8_t checksum = 0;
	size_t i;
	for (i = 0x134; i < 0x14D; ++i) {
		checksum = checksum - booted[i] - 1;
	}
	if (booted[0x14D] != checksum) {
		printf("FAIL: boot ROM sees a bad header checksum\n");
		++failures;
	}

	gb->memory.io[GB_REG_BANK] = 0xFF;
	GBUnmapBIOS(gb);
	if (gb->memory.romBase[0x14D] != 0x5A || gb->memory.romBase[0x108] != 0) {
		printf("FAIL: game no longer reads its own header\n");
		++failures;
	}
	core->deinit(core);

	// Sachen mappers scramble the boot ROM's header reads, so a real cart
	// passes with bytes that look wrong here; repairing them makes it hang.
	memset(rom, 0, sizeof(rom));
	rom[0x104] = 0xCE;
	rom[0x144] = 0xED;
	rom[0x114] = 0x66;
	rom[0x154] = 0x66;
	core = GBCoreCreate();
	core->init(core);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	gb = core->board;
	gb->biosVf = VFileFromConstMemory(bios, sizeof(bios));
	GBMapBIOS(gb);
	if (memcmp(&gb->memory.romBase[0x100], &rom[0x100], 0x50) != 0) {
		printf("FAIL: Sachen header was repaired\n");
		++failures;
	}
	gb->memory.io[GB_REG_BANK] = 0xFF;
	GBUnmapBIOS(gb);
	core->deinit(core);

	printf("4 checks, %d failures\n", failures);
	return failures != 0;
}
