/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A ROM whose header logo or checksum was damaged by a patch must still boot
// through a real BIOS: the core repairs both in memory rather than skipping
// the boot animation, since the BIOS itself hangs on either.

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/crc32.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <string.h>

static uint32_t bios[GBA_SIZE_BIOS / 4];
static int failures = 0;

static uint8_t headerChecksum(const uint8_t* rom) {
	uint8_t checksum = (uint8_t) -0x19;
	size_t i;
	for (i = 0xA0; i < 0xBD; ++i) {
		checksum -= rom[i];
	}
	return checksum;
}

// Boots rom and checks the BIOS was not skipped and sees a valid header,
// leaving that header in `booted`.
static void boot(const char* name, const uint8_t* rom, size_t size, uint8_t* booted) {
	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, size));
	core->loadBIOS(core, VFileFromConstMemory(bios, sizeof(bios)), 0);
	core->reset(core);

	struct GBA* gba = core->board;
	const uint8_t* header = (const uint8_t*) gba->memory.rom;
	uint32_t pc = gba->cpu->gprs[ARM_PC];
	if (pc >> 24) {
		printf("FAIL: %s: BIOS skipped, pc=%08X\n", name, pc);
		++failures;
	}
	if (doCrc32(&header[4], 0x9C) != 0xD0BEB55E) {
		printf("FAIL: %s: logo not repaired\n", name);
		++failures;
	}
	if (header[0xBD] != headerChecksum(header)) {
		printf("FAIL: %s: header checksum not repaired\n", name);
		++failures;
	}
	memcpy(booted, header, 0xC0);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

int main(void) {
	static uint8_t rom[0x200];
	static uint8_t booted[0xC0];

	// Seven vectors of branches is all GBAIsBIOS asks of a BIOS image.
	int i;
	for (i = 0; i < 7; ++i) {
		bios[i] = 0xEA000000;
	}

	// A zeroed header: both the logo and the checksum are wrong.
	boot("bad logo", rom, sizeof(rom), booted);

	// An intact logo with only the checksum wrong, as a retitled romhack.
	memcpy(rom, booted, 0xA0);
	rom[0xBD] = headerChecksum(rom) + 1;
	boot("bad checksum", rom, sizeof(rom), booted);

	printf("6 checks, %d failures\n", failures);
	return failures != 0;
}
