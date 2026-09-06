/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression tests for GBMBCSwitchSramBank(). MBC2 and MBC7 allocate a
// 0x100-byte save and TAMA5 a 0x20-byte one, all smaller than the 8KB bank the
// bounds check is written in terms of. That used to reject even bank 0 and
// return without assigning sramBank, leaving it pointing at whatever the
// previously loaded cartridge had mapped -- a use-after-free the next time the
// CPU read through it.
//
// Links against the real src/gb/mbc.c, so a regression in the shipped code
// fails here. See libretro-build/run_tests.sh for how to build and run it.

#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/mbc.h>

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

// GBMBCInit() references every MBC handler, which drags in the rest of the GB
// core. The function under test touches only gb->memory and gb->sramSize, so
// the remaining symbols are stubbed in stubs.c. These two live here because
// they are declared in headers the test includes.
void GBResizeSram(struct GB* gb, size_t size) {
	(void) gb;
	(void) size;
}

bool GBIsROM(struct VFile* vf) {
	(void) vf;
	return false;
}

static uint8_t previousCart[GB_SIZE_EXTERNAL_RAM];
static uint8_t currentCart[0x100];
static uint8_t regularCart[GB_SIZE_EXTERNAL_RAM * 4];

// Reproduces the crash: play a cartridge with a full-size save, swap to one
// whose save is smaller than a bank, then load a savestate (which calls the
// bank switch unconditionally via GBMemoryDeserialize).
static void testSwapToSubBankCart(const char* name, size_t sramSize) {
	struct GB gb;
	memset(&gb, 0, sizeof(gb));
	gb.memory.sram = previousCart;
	gb.memory.sramBank = previousCart;
	gb.sramSize = GB_SIZE_EXTERNAL_RAM;

	// The swap: sram now points at the new cartridge, sramBank still at the old
	// mapping, which by this point has been unmapped.
	gb.memory.sram = currentCart;
	gb.sramSize = sramSize;

	GBMBCSwitchSramBank(&gb, 0);

	char message[128];
	snprintf(message, sizeof(message), "%s (0x%zX): sramBank lands inside the current save", name, sramSize);
	ok(message, gb.memory.sramBank == currentCart);

	snprintf(message, sizeof(message), "%s: sramBank no longer points at the unloaded cartridge", name);
	ok(message, gb.memory.sramBank != previousCart);

	snprintf(message, sizeof(message), "%s: bank index reset to 0", name);
	ok(message, gb.memory.sramCurrentBank == 0);
}

int main(void) {
	memset(previousCart, 0xAA, sizeof(previousCart));
	memset(currentCart, 0xBB, sizeof(currentCart));
	memset(regularCart, 0xCC, sizeof(regularCart));

	printf("[saves smaller than one bank: switching after a cartridge swap]\n");
	testSwapToSubBankCart("MBC2", 0x100);
	testSwapToSubBankCart("MBC7", 0x100);
	testSwapToSubBankCart("TAMA5", 0x20);

	// GBX carries ramSize as a 32-bit footer field that nothing validates, so a
	// crafted or corrupt file can name any size at all.
	printf("\n[an arbitrary GBX ramSize must not escape the save]\n");
	static const size_t sizes[] = { 1, 2, 0x20, 0x100, 0x1FFF };
	for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
		struct GB gb;
		memset(&gb, 0, sizeof(gb));
		gb.memory.sram = currentCart;
		gb.memory.sramBank = previousCart;
		gb.sramSize = sizes[i];

		GBMBCSwitchSramBank(&gb, 0);

		char message[128];
		snprintf(message, sizeof(message), "ramSize=0x%zX keeps the window at the start of the save", sizes[i]);
		ok(message, gb.memory.sramBank == currentCart);
	}

	printf("\n[an unmapped save must not leave a stale pointer behind]\n");
	{
		struct GB gb;
		memset(&gb, 0, sizeof(gb));
		gb.memory.sram = NULL;
		gb.memory.sramBank = previousCart;
		gb.sramSize = 0;

		GBMBCSwitchSramBank(&gb, 0);
		ok("sramBank cleared when sram is NULL", gb.memory.sramBank == NULL);
	}
	{
		// GBResizeSram() zeroes sramSize on a failed mapping, but a caller can
		// still reach the switch with the pair briefly inconsistent.
		struct GB gb;
		memset(&gb, 0, sizeof(gb));
		gb.memory.sram = NULL;
		gb.memory.sramBank = previousCart;
		gb.sramSize = GB_SIZE_EXTERNAL_RAM;

		GBMBCSwitchSramBank(&gb, 0);
		ok("sramBank cleared when the mapping failed but sramSize is non-zero", gb.memory.sramBank == NULL);
	}

	printf("\n[regression: ordinary 32KB saves still switch banks]\n");
	for (int bank = 0; bank < 4; ++bank) {
		struct GB gb;
		memset(&gb, 0, sizeof(gb));
		gb.memory.sram = regularCart;
		gb.memory.sramBank = regularCart;
		gb.sramSize = sizeof(regularCart);

		GBMBCSwitchSramBank(&gb, bank);

		char message[128];
		snprintf(message, sizeof(message), "bank %d maps to offset 0x%X", bank, bank * GB_SIZE_EXTERNAL_RAM);
		ok(message, gb.memory.sramBank == &regularCart[bank * GB_SIZE_EXTERNAL_RAM]
		    && gb.memory.sramCurrentBank == bank);
	}

	printf("\n[regression: out-of-range banks are still rejected]\n");
	{
		struct GB gb;
		memset(&gb, 0, sizeof(gb));
		gb.memory.sram = regularCart;
		gb.memory.sramBank = regularCart;
		gb.sramSize = sizeof(regularCart);

		GBMBCSwitchSramBank(&gb, 99);
		ok("bank 99 cannot point past the end of the save",
		    gb.memory.sramBank >= regularCart
		    && gb.memory.sramBank + GB_SIZE_EXTERNAL_RAM <= regularCart + sizeof(regularCart));
	}

	printf("\n%d checks, %d failed\n", checks, failures);
	return failures != 0;
}
