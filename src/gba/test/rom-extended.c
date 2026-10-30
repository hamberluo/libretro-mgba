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
// A made-up 512 KiB flash part in their chip table must answer its ID and
// keep banks past 1 across a savestate; without one, flash stays 128 KiB.

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

static void flashCommand(struct mCore* core, uint8_t command) {
	core->busWrite8(core, 0x0E005555, 0xAA);
	core->busWrite8(core, 0x0E002AAA, 0x55);
	core->busWrite8(core, 0x0E005555, command);
}

static uint32_t flashBank(struct mCore* core) {
	struct GBASavedata* savedata = &((struct GBA*) core->board)->memory.savedata;
	return (savedata->currentBank - savedata->data) >> 16;
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
	struct GBASavedata* savedata = &((struct GBA*) core->board)->memory.savedata;
	GBASavedataForceType(savedata, GBA_SAVEDATA_FLASH1M);
	expect("64M plain flash size", GBASavedataSize(savedata), 0x20000);
	unload(core);

	// agb_flash FlashType: romSize, sector size/shift/count/top, wait, ID.
	memcpy(&rom[0x100000], "\x00\x00\x08\x00\x00\x10\x00\x00\x0C\x00\x80\x00\x00\x00\x00\x00\x03\x00\x01\x00\x47\x53", 22);
	core = load(rom, GBA_SIZE_ROM_EXTENDED);
	savedata = &((struct GBA*) core->board)->memory.savedata;
	GBASavedataForceType(savedata, GBA_SAVEDATA_FLASH1M);
	expect("64M flash size", GBASavedataSize(savedata), 0x80000);
	flashCommand(core, 0x90);
	expect("64M flash ID", core->busRead8(core, 0x0E000000) | core->busRead8(core, 0x0E000001) << 8, 0x5347);
	flashCommand(core, 0xF0);
	flashCommand(core, 0xB0);
	core->busWrite8(core, 0x0E000000, 7);
	flashCommand(core, 0xA0);
	core->busWrite8(core, 0x0E000000, 0x66);
	expect("64M flash bank 7", savedata->data[0x70000], 0x66);
	state = VFileMemChunk(NULL, 0);
	mCoreSaveStateNamed(core, state, 0);
	flashCommand(core, 0xB0);
	core->busWrite8(core, 0x0E000000, 0);
	state->seek(state, 0, SEEK_SET);
	mCoreLoadStateNamed(core, state, 0);
	state->close(state);
	expect("64M flash bank state", flashBank(core), 7);
	unload(core);

	// Not a power of two: padded out to the extended bus like a flash cart.
	core = load(rom, GBA_SIZE_ROM0 + 0x100);
	expect("odd-size ROM1", core->busRead32(core, 0x0A000000), 0x22222222);
	unload(core);

	core = load(rom, GBA_SIZE_ROM0);
	expect("32M ROM1 mirror", core->busRead32(core, 0x0A000000), 0x11111111);
	core->busWrite32(core, 0x02040000, 0x55555555);
	expect("32M EWRAM mirror", core->busRead32(core, 0x02000000), 0x55555555);
	GBASavedataForceType(&((struct GBA*) core->board)->memory.savedata, GBA_SAVEDATA_FLASH1M);
	expect("32M flash size", GBASavedataSize(&((struct GBA*) core->board)->memory.savedata), 0x20000);
	flashCommand(core, 0xB0);
	core->busWrite8(core, 0x0E000000, 7);
	expect("32M flash bank 7 refused", flashBank(core), 0);
	unload(core);

	// Bootleg multicarts are this big too, but bank through their own registers.
	memcpy(&rom[0xAC], "AXVJ01", 6);
	core = load(rom, GBA_SIZE_ROM_EXTENDED);
	expect("multicart ROM1 mirror", core->busRead32(core, 0x0A000000), 0x11111111);
	core->busWrite32(core, 0x02040000, 0x55555555);
	expect("multicart EWRAM mirror", core->busRead32(core, 0x02000000), 0x55555555);
	unload(core);

	free(rom);
	printf("25 checks, %d failures\n", failures);
	return failures != 0;
}
