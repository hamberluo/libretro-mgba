/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef REALTIME_BGM_GB_TEST_ROM_H
#define REALTIME_BGM_GB_TEST_ROM_H

// A 64 KB MBC5 cartridge that spins at 0x150, with a HALT byte for parking and,
// in bank 1, a stub sound engine counting its calls at WRAM 0xD000. Tests write
// the engine's RAM themselves, as the game's sound code would.

#include <stdint.h>
#include <string.h>

#define GB_ROM_SIZE 0x10000
#define GB_STUB_ENGINE 0x4000 // bank 1: ld hl,$D000; inc [hl]; ret
#define GB_STUB_SPIN 0x4010   // bank 1: jr -2
#define GB_STUB_CALLS 0xD000

static inline void gbBuildRom(uint8_t* rom) {
	memset(rom, 0, GB_ROM_SIZE);
	static const uint8_t entry[] = { 0x00, 0xC3, 0x50, 0x01 }; // nop; jp $0150
	memcpy(&rom[0x100], entry, sizeof(entry));
	rom[0x147] = 0x19; // MBC5
	rom[0x148] = 0x01; // 64 KB
	rom[0x150] = 0x18; rom[0x151] = 0xFE; // jr -2
	rom[0x152] = 0x76;                    // halt
	static const uint8_t engine[] = { 0x21, 0x00, 0xD0, 0x34, 0xC9 };
	memcpy(&rom[GB_STUB_ENGINE], engine, sizeof(engine));
	rom[GB_STUB_SPIN] = 0x18; rom[GB_STUB_SPIN + 1] = 0xFE;
}

#endif
