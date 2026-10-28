/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Romhacks past 32 MiB read the rest through 0x0A000000 as one linear 64 MiB
// bus. Ordinary ROMs must keep mirroring there, a ROM patch (cheat) on an
// extended ROM must copy the whole image, not the first 32 MiB, and a cheat
// hook there must keep its address, since it fires on an exact PC match.
// Their EWRAM past 256 KiB must not mirror, and must survive a savestate.

#include <mgba/core/cheats.h>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/cheats.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>
#include "gba/cheats/gameshark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect(const char* name, uint32_t got, uint32_t want) {
	if (got != want) {
		printf("FAIL: %s: %08X, want %08X\n", name, got, want);
		++failures;
	}
}

static struct mCore* load(uint8_t* rom, size_t size) {
	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, size));
	core->reset(core);
	return core;
}

static void unload(struct mCore* core) {
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

int main(void) {
	uint8_t* rom = calloc(1, GBA_SIZE_ROM_EXTENDED);
	memcpy(&rom[0], "\x11\x11\x11\x11", 4);
	memcpy(&rom[GBA_SIZE_ROM0], "\x22\x22\x22\x22", 4);

	struct mCore* core = load(rom, GBA_SIZE_ROM_EXTENDED);
	expect("64M ROM0", core->busRead32(core, 0x08000000), 0x11111111);
	expect("64M ROM1", core->busRead32(core, 0x0A000000), 0x22222222);
	expect("64M ROM1 16", core->busRead16(core, 0x0A000000), 0x2222);
	expect("64M ROM1 8", core->busRead8(core, 0x0A000000), 0x22);
	int32_t old;
	GBAPatch32(((struct GBA*) core->board)->cpu, 0x0A000000, 0x33333333, &old);
	expect("patch old", old, 0x22222222);
	expect("patched ROM1", core->busRead32(core, 0x0A000000), 0x33333333);
	expect("patched ROM0", core->busRead32(core, 0x08000000), 0x11111111);
	struct mCheatDevice* device = core->cheatDevice(core);
	struct GBACheatSet* set = (struct GBACheatSet*) device->createSet(device, NULL);
	GBACheatAddGameSharkRaw(set, 0xFA000100, 0x00000001);
	expect("GameShark hook", set->hook->address, 0x0A000100);
	mCheatSetDeinit(&set->d);
	set = (struct GBACheatSet*) device->createSet(device, NULL);
	GBACheatAddCodeBreaker(set, 0x1A000100, 0x0001);
	expect("CodeBreaker hook", set->hook->address, 0x0A000100);
	mCheatSetDeinit(&set->d);
	core->busWrite32(core, 0x02000000, 0x44444444);
	core->busWrite32(core, 0x02040000, 0x55555555);
	expect("64M EWRAM low", core->busRead32(core, 0x02000000), 0x44444444);
	struct VFile* state = VFileMemChunk(NULL, 0);
	mCoreSaveStateNamed(core, state, 0);
	core->busWrite32(core, 0x02040000, 0);
	state->seek(state, 0, SEEK_SET);
	mCoreLoadStateNamed(core, state, 0);
	state->close(state);
	expect("64M EWRAM high state", core->busRead32(core, 0x02040000), 0x55555555);
	unload(core);

	// Not a power of two: padded out to the extended bus like a flash cart.
	core = load(rom, GBA_SIZE_ROM0 + 0x100);
	expect("odd-size ROM1", core->busRead32(core, 0x0A000000), 0x22222222);
	unload(core);

	core = load(rom, GBA_SIZE_ROM0);
	expect("32M ROM1 mirror", core->busRead32(core, 0x0A000000), 0x11111111);
	core->busWrite32(core, 0x02040000, 0x55555555);
	expect("32M EWRAM mirror", core->busRead32(core, 0x02000000), 0x55555555);
	unload(core);

	// Bootleg multicarts are this big too, but bank through their own registers.
	memcpy(&rom[0xAC], "AXVJ01", 6);
	core = load(rom, GBA_SIZE_ROM_EXTENDED);
	expect("multicart ROM1 mirror", core->busRead32(core, 0x0A000000), 0x11111111);
	core->busWrite32(core, 0x02040000, 0x55555555);
	expect("multicart EWRAM mirror", core->busRead32(core, 0x02000000), 0x55555555);
	unload(core);

	free(rom);
	printf("17 checks, %d failures\n", failures);
	return failures != 0;
}
