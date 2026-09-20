/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for the write half of a cheat.
//
// Every other cheat test stops at the parse: it asserts the cheat list holds
// the right address and operand. Nothing asserted that `mCheatRefresh` then
// puts the operand in memory, which is what makes a cheat visible in the game
// and the half a "the code is accepted but nothing happens" report points at.
//
// The core here is a stub whose bus reads and writes land in a flat EWRAM
// array, so an assertion can read back what the refresh wrote.

#include "platform/libretro/cheat-split.h"

#include <mgba/core/core.h>
#include <mgba/core/cheats.h>
#include <mgba/internal/gba/cheats.h>
#include <mgba/internal/gba/memory.h>

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

static uint8_t ewram[GBA_SIZE_EWRAM];

static bool inEwram(uint32_t address) {
	return address >= GBA_BASE_EWRAM &&
	       address + 2 <= GBA_BASE_EWRAM + GBA_SIZE_EWRAM;
}

static uint32_t _busRead16(struct mCore* core, uint32_t address) {
	UNUSED(core);
	if (!inEwram(address)) {
		return 0;
	}
	const uint8_t* p = &ewram[address - GBA_BASE_EWRAM];
	return p[0] | (p[1] << 8);
}

static void _busWrite16(struct mCore* core, uint32_t address, uint16_t value) {
	UNUSED(core);
	if (!inEwram(address)) {
		return;
	}
	uint8_t* p = &ewram[address - GBA_BASE_EWRAM];
	p[0] = value;
	p[1] = value >> 8;
}

static struct mCore stubCore;

static void resetCore(void) {
	memset(ewram, 0, sizeof(ewram));
	memset(&stubCore, 0, sizeof(stubCore));
	stubCore.busRead16 = _busRead16;
	stubCore.busWrite16 = _busWrite16;
}

// Builds a device holding `code`, wired to the stub core the way
// _GBACoreCheatDevice wires the real one.
static struct mCheatDevice* deviceFor(const char* code, struct mCheatSet** outSet) {
	struct mCheatDevice* device = GBACheatDeviceCreate();
	device->p = &stubCore;
	struct mCheatSet* set = device->createSet(device, NULL);
	mCheatAddSet(device, set);
	char line[64];
	while ((code = retroCheatNextLine(code, line, sizeof(line)))) {
		mCheatAddLine(set, line, 0);
	}
	*outSet = set;
	return device;
}

static uint16_t read16(uint32_t address) {
	return _busRead16(&stubCore, address);
}

int main(void) {
	// A 16-bit CodeBreaker write is the shape a user types by hand, and the
	// one the field report used: 9999 coins into EWRAM.
	resetCore();
	struct mCheatSet* set;
	struct mCheatDevice* device = deviceFor("82002DB0 270F", &set);
	ok("precondition: the code parsed into one cheat",
	   mCheatListSize(&set->list) == 1);
	ok("precondition: memory starts clear", read16(0x02002DB0) == 0);
	mCheatRefresh(device, set);
	ok("a refresh writes the operand to EWRAM", read16(0x02002DB0) == 0x270F);

	// The game overwrites the address every frame; the cheat has to win each
	// time, which is why the refresh runs per frame rather than once.
	_busWrite16(&stubCore, 0x02002DB0, 0x0001);
	mCheatRefresh(device, set);
	ok("a later refresh writes it again over the game's own value",
	   read16(0x02002DB0) == 0x270F);

	// Disabling the set is what the frontend does for a switched-off cheat.
	set->enabled = false;
	_busWrite16(&stubCore, 0x02002DB0, 0x0001);
	mCheatRefresh(device, set);
	ok("a disabled set writes nothing", read16(0x02002DB0) == 0x0001);
	mCheatDeviceDestroy(device);

	// The `+` spelling this frontend now stores must write the same value as
	// the joined one a hand-typed code used to become.
	resetCore();
	struct mCheatSet* plusSet;
	struct mCheatDevice* plus = deviceFor("82002DB0+270F", &plusSet);
	mCheatRefresh(plus, plusSet);
	uint16_t viaPlus = read16(0x02002DB0);
	mCheatDeviceDestroy(plus);

	resetCore();
	struct mCheatSet* joinedSet;
	struct mCheatDevice* joined = deviceFor("82002DB0270F", &joinedSet);
	mCheatRefresh(joined, joinedSet);
	uint16_t viaJoined = read16(0x02002DB0);
	mCheatDeviceDestroy(joined);

	ok("the + and joined spellings write the same value",
	   viaPlus == 0x270F && viaJoined == 0x270F);

	printf("\n%d/%d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
