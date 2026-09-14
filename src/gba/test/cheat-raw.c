/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for raw "address value" cheat lines under autodetect.
//
// Pokémon hack communities publish plain writes as "AAAAAAAA VVVV" (16-bit)
// and "AAAAAAAA VVVVVVVV" (32-bit). The first shape is also CodeBreaker's,
// where a leading 0 nibble means "game ID" -- a line mGBA accepts and then
// ignores, so the cheat reported success and did nothing. A real CodeBreaker
// game ID never carries a region byte (its address is 0000XXXX), so a leading
// 0 over a real GBA address can only be a raw write. Every test here goes
// through the libretro splitter into the real GBA parser.

#include "platform/libretro/cheat-split.h"

#include <mgba/core/cheats.h>
#include <mgba/internal/gba/cheats.h>
#include "gba/cheats/gameshark.h"

#include <stdio.h>

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

static void feed(struct mCheatSet* set, const char* code) {
	char line[64];
	while ((code = retroCheatNextLine(code, line, sizeof(line)))) {
		mCheatAddLine(set, line, 0);
	}
}

static struct mCheatSet* parse(struct mCheatDevice* device, const char* code) {
	struct mCheatSet* set = device->createSet(device, NULL);
	mCheatAddSet(device, set);
	feed(set, code);
	return set;
}

static int isWrite(struct mCheatSet* set, size_t i, uint32_t address, int width, uint32_t value) {
	if (i >= mCheatListSize(&set->list)) {
		return 0;
	}
	struct mCheat* c = mCheatListGetPointer(&set->list, i);
	return c->type == CHEAT_ASSIGN && c->address == address && c->width == width && c->operand == value;
}

static int isPatch(struct mCheatSet* set, size_t i, uint32_t address, int width, uint32_t value) {
	if (i >= mCheatPatchListSize(&set->romPatches)) {
		return 0;
	}
	struct mCheatPatch* p = mCheatPatchListGetPointer(&set->romPatches, i);
	return p->address == address && p->width == width && p->value == value;
}

int main(void) {
	struct mCheatDevice* device = GBACheatDeviceCreate();

	// The field report: a 16-bit RAM write stored compactly.
	struct mCheatSet* ram = parse(device, "020385940000");
	ok("a 16-bit raw RAM write is a cheat, not a CodeBreaker game ID",
	   mCheatListSize(&ram->list) == 1 && isWrite(ram, 0, 0x02038594, 2, 0x0000));

	// Two 16-bit ROM writes; the values are Thumb instructions being patched in.
	struct mCheatSet* rom = parse(device, "0806E8F02201+0806ED262001");
	ok("16-bit raw ROM writes become ROM patches",
	   mCheatListSize(&rom->list) == 0 && mCheatPatchListSize(&rom->romPatches) == 2 &&
	   isPatch(rom, 0, 0x0806E8F0, 2, 0x2201) && isPatch(rom, 1, 0x0806ED26, 2, 0x2001));

	// A 32-bit ROM write has GameShark's shape; as GameShark it is a byte
	// write into ROM, which no GameShark code ever means.
	struct mCheatSet* rom32 = parse(device, "08499B8C0032031F");
	ok("a 32-bit raw ROM write becomes a ROM patch",
	   mCheatPatchListSize(&rom32->romPatches) == 1 && isPatch(rom32, 0, 0x08499B8C, 4, 0x0032031F));

	// libretro keeps every code in one set, so an encrypted PARv3 code enabled
	// earlier pins the set's GameShark version. The raw write must not be
	// decrypted with those seeds.
	struct mCheatSet* mixed = parse(device, "7881A409E2026E0C+8E883EFF92E9660D+8173E2E87E090FC0");
	ok("precondition: the set is pinned to PARv3", ((struct GBACheatSet*) mixed)->gsaVersion == GBA_GS_PARV3);
	size_t before = mCheatPatchListSize(&mixed->romPatches);
	feed(mixed, "08499B8C0032031F");
	ok("a raw ROM write survives a set already pinned to PARv3",
	   mCheatPatchListSize(&mixed->romPatches) == before + 1 && isPatch(mixed, before, 0x08499B8C, 4, 0x0032031F));

	// A genuine CodeBreaker game ID has no region byte and must stay inert.
	struct mCheatSet* gameId = parse(device, "00008E9A000A");
	ok("a CodeBreaker game ID still produces nothing",
	   mCheatListSize(&gameId->list) == 0 && mCheatPatchListSize(&gameId->romPatches) == 0);

	// GameShark's raw 8-bit write shares the 32-bit shape over RAM; it must
	// keep its width.
	struct mCheatSet* gs = parse(device, "02024EA000000010");
	ok("a GameShark 8-bit RAM write keeps its width",
	   mCheatListSize(&gs->list) == 1 && isWrite(gs, 0, 0x02024EA0, 1, 0x10));

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
