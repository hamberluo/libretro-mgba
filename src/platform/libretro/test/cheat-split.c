/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for the libretro cheat line splitter.
//
// retro_cheat_set used to cut the incoming string at fixed offsets 13 and 17
// instead of at the `+` between lines. A frontend that stores a multi-line code
// compactly ("AAAAAAAABBBBBBBB+...") desynchronized after the first line: the
// second arrived shifted by one character and the third never reached the
// parser at all. Every test here feeds the split lines to the real GBA parser,
// so it checks what the core ends up with, not just the strings.

#include "platform/libretro/cheat-split.h"

#include <mgba/core/cheats.h>
#include <mgba/internal/gba/cheats.h>
#include "gba/cheats/gameshark.h"

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

// Splits `code` the way retro_cheat_set does and returns the resulting set.
static struct mCheatSet* parse(struct mCheatDevice* device, const char* code) {
	struct mCheatSet* set = device->createSet(device, NULL);
	mCheatAddSet(device, set);
	char line[64];
	while ((code = retroCheatNextLine(code, line, sizeof(line)))) {
		mCheatAddLine(set, line, 0);
	}
	return set;
}

int main(void) {
	struct mCheatDevice* device = GBACheatDeviceCreate();

	// The three-line PARv3 code from the field report, stored the compact way.
	struct mCheatSet* par = parse(device, "7881A409E2026E0C+8E883EFF92E9660D+8173E2E87E090FC0");
	ok("three compact lines all reach the parser", mCheatListSize(&par->list) == 1 && mCheatPatchListSize(&par->romPatches) == 1);
	ok("PARv3 is detected", ((struct GBACheatSet*) par)->gsaVersion == GBA_GS_PARV3);
	ok("the RAM write decrypts to EWRAM", mCheatListGetPointer(&par->list, 0)->address == 0x0203735B);
	ok("the ROM patch decrypts to ROM", mCheatPatchListGetPointer(&par->romPatches, 0)->address == 0x08092C06);

	// One code, spaced and compact, must produce the same cheat.
	struct mCheatSet* spaced = parse(device, "020375D0 00000063");
	struct mCheatSet* compact = parse(device, "020375D000000063");
	ok("spaced and compact single codes agree",
	   mCheatListSize(&spaced->list) == 1 && mCheatListSize(&compact->list) == 1 &&
	   mCheatListGetPointer(&spaced->list, 0)->address == mCheatListGetPointer(&compact->list, 0)->address &&
	   mCheatListGetPointer(&spaced->list, 0)->operand == mCheatListGetPointer(&compact->list, 0)->operand);

	// CodeBreaker codes in libretro `.cht` files put the address and the value
	// in separate `+` segments ("320375D4+0000" is the single line
	// "320375D4 0000"), so an 8-digit segment followed by a 4-digit one is one
	// line, not two. GameShark's 16-bit writes share that shape and are also
	// one line, so pairing them is right for both.
	struct mCheatSet* cb = parse(device, "320375D4+0000");
	ok("a CodeBreaker address/value pair is one line", mCheatListSize(&cb->list) == 1);

	struct mCheatSet* cbSpaced = parse(device, "320375D4 0000");
	ok("the split pair matches the spaced form",
	   mCheatListSize(&cb->list) == 1 && mCheatListSize(&cbSpaced->list) == 1 &&
	   mCheatListGetPointer(&cb->list, 0)->address == mCheatListGetPointer(&cbSpaced->list, 0)->address &&
	   mCheatListGetPointer(&cb->list, 0)->operand == mCheatListGetPointer(&cbSpaced->list, 0)->operand);

	// Two CodeBreaker lines, four segments: Max Money from the Emerald pack.
	struct mCheatSet* cbTwo = parse(device, "82000568+423F+8200056A+000F");
	ok("four segments become two CodeBreaker lines", mCheatListSize(&cbTwo->list) == 2);

	// GameShark splits an address from its value the same way, so an 8+8 pair
	// is one line too -- on its own, an eight-digit segment parses as nothing.
	struct mCheatSet* gs = parse(device, "D8BAE4D9+4864DCE5");
	struct mCheatSet* gsSpaced = parse(device, "D8BAE4D9 4864DCE5");
	ok("a GameShark address/value pair is one line",
	   mCheatListSize(&gs->list) == mCheatListSize(&gsSpaced->list) &&
	   mCheatPatchListSize(&gs->romPatches) == mCheatPatchListSize(&gsSpaced->romPatches));

	// The Emerald master code: four segments, two GameShark lines.
	struct mCheatSet* master = parse(device, "D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3");
	ok("the master code parses", ((struct GBACheatSet*) master)->gsaVersion != 0);

	// Half of a GameShark v3 code parses but produces nothing: the master code
	// only seeds the decryption. Every standalone format produces an entry, so
	// an empty result is what tells the two apart.
	struct mCheatSet* half = parse(device, "D8BAE4D9+4864DCE5");
	ok("half a multi-line code produces nothing",
	   mCheatListSize(&half->list) == 0 && mCheatPatchListSize(&half->romPatches) == 0);

	struct mCheatSet* whole = parse(device,
		"D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3+A57E2EDE+A5AFF3E4+"
		"1C7B3231+B494738C+C051CCF6+975E8DA1");
	ok("the whole code produces a cheat and a patch",
	   mCheatListSize(&whole->list) == 1 && mCheatPatchListSize(&whole->romPatches) == 1);

	struct mCheatSet* standalone = parse(device, "320375D4+0000");
	ok("a standalone code produces an entry", mCheatListSize(&standalone->list) == 1);

	// Splitter edge cases.
	char line[8];
	ok("empty string yields no line", retroCheatNextLine("", line, sizeof(line)) == NULL);
	ok("lone separator yields no line", retroCheatNextLine("+", line, sizeof(line)) == NULL);
	const char* rest = retroCheatNextLine("ABC+", line, sizeof(line));
	ok("trailing separator yields one line", rest && strcmp(line, "ABC") == 0 && retroCheatNextLine(rest, line, sizeof(line)) == NULL);
	retroCheatNextLine("0123456789", line, sizeof(line));
	ok("an overlong line is truncated, not overrun", strcmp(line, "0123456") == 0);
	retroCheatNextLine("AAAA\tBBBB", line, sizeof(line));
	ok("inner whitespace becomes a space", strcmp(line, "AAAA BB") == 0);

	mCheatDeviceDestroy(device);
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
