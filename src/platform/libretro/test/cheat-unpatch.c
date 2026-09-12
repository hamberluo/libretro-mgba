/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for ROM patches outliving the cheats that installed them.
//
// A GameShark v3 master code patches the ROM as well as writing RAM, and
// mCheatDeviceClear used to drop the cheat sets without reverting those
// patches. retro_cheat_reset calls it before every apply, so switching a cheat
// off left the ROM modified for the rest of the session -- on a ROM the code
// was not written for, a broken instruction and a white screen that only
// killing the process could clear.

#include <mgba/core/cheats.h>
#include <mgba/core/core.h>
#include <mgba/internal/gba/cheats.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

static void ok(const char* what, int condition) {
	++checks;
	if (condition) {
		printf("  ok    %s\n", what);
	} else {
		++failures;
		printf("  FAIL  %s\n", what);
	}
}

// A core that is nothing but addressable memory, so a patch and its revert are
// both visible as plain reads.
#define FAKE_BASE 0x08000000
#define FAKE_SIZE 0x200

static uint16_t fakeRom[FAKE_SIZE];

static uint32_t fakeRawRead16(struct mCore* core, uint32_t address, int segment) {
	(void) core;
	(void) segment;
	uint32_t offset = (address - FAKE_BASE) >> 1;
	return offset < FAKE_SIZE ? fakeRom[offset] : 0;
}

static void fakeRawWrite16(struct mCore* core, uint32_t address, int segment, uint16_t value) {
	(void) core;
	(void) segment;
	uint32_t offset = (address - FAKE_BASE) >> 1;
	if (offset < FAKE_SIZE) {
		fakeRom[offset] = value;
	}
}

int main(void) {
	// The instruction the Emerald master code overwrites, as it sits in a ROM
	// the code was not written for.
	const uint32_t patchAddress = FAKE_BASE + 0x20;
	const uint16_t original = 0xFCF1;
	memset(fakeRom, 0, sizeof(fakeRom));
	fakeRom[0x20 >> 1] = original;

	struct mCore core = {0};
	core.rawRead16 = fakeRawRead16;
	core.rawWrite16 = fakeRawWrite16;

	struct mCheatDevice* device = GBACheatDeviceCreate();
	device->p = &core;

	struct mCheatSet* set = device->createSet(device, NULL);
	mCheatAddSet(device, set);

	// One ROM patch, the shape a GameShark v3 master code produces.
	struct mCheatPatch* patch = mCheatPatchListAppend(&set->romPatches);
	patch->address = patchAddress;
	patch->segment = -1;
	patch->width = 2;
	patch->value = 0x2400;
	patch->applied = false;
	patch->check = false;

	set->enabled = true;
	mCheatRefresh(device, set);
	ok("the patch reaches the ROM", fakeRom[0x20 >> 1] == 0x2400);

	// retro_cheat_reset does exactly this before re-applying what is left.
	mCheatDeviceClear(device);
	ok("clearing the device puts the ROM back", fakeRom[0x20 >> 1] == original);

	// What the frontend actually does when a cheat is switched off: clear, then
	// apply what is left. The ROM must end up untouched, not patched again.
	struct mCheatSet* rest = device->createSet(device, NULL);
	mCheatAddSet(device, rest);
	rest->enabled = true;
	mCheatRefresh(device, rest);
	ok("re-applying without the patch leaves the ROM alone", fakeRom[0x20 >> 1] == original);

	// And switching it back on patches again, so the revert did not lose the
	// bookkeeping that makes a second apply possible.
	struct mCheatSet* again = device->createSet(device, NULL);
	mCheatAddSet(device, again);
	struct mCheatPatch* patch2 = mCheatPatchListAppend(&again->romPatches);
	patch2->address = patchAddress;
	patch2->segment = -1;
	patch2->width = 2;
	patch2->value = 0x2400;
	patch2->applied = false;
	patch2->check = false;
	again->enabled = true;
	mCheatRefresh(device, again);
	ok("the patch can be applied a second time", fakeRom[0x20 >> 1] == 0x2400);

	// Destroying the device is the unload path, and it must revert too.
	mCheatDeviceDestroy(device);
	ok("destroying the device puts the ROM back", fakeRom[0x20 >> 1] == original);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
