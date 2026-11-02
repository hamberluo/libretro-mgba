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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GB_ROM_SIZE 0x10000
#define GB_STUB_ENGINE 0x4000 // bank 1: ld hl,$D000; inc [hl]; ret
#define GB_STUB_SPIN 0x4010   // bank 1: jr -2
#define GB_STUB_SLOW 0x4020   // bank 1: GB_STUB_ENGINE, then ~4k cycles on odd calls
#define GB_STUB_CALLS 0xD000

// Audible: before spinning, the cartridge starts a square wave on channel 2.
static inline void gbBuildRom(uint8_t* rom, bool audible) {
	memset(rom, 0, GB_ROM_SIZE);
	static const uint8_t entry[] = { 0x00, 0xC3, 0x50, 0x01 }; // nop; jp $0150
	memcpy(&rom[0x100], entry, sizeof(entry));
	static const uint8_t logoStart[] = { 0xCE, 0xED, 0x66, 0x66 }; // what GBIsROM checks
	memcpy(&rom[0x104], logoStart, sizeof(logoStart));
	rom[0x147] = 0x19; // MBC5
	rom[0x148] = 0x01; // 64 KB
	// ld a,n; ldh [r],a for NR52, NR50, NR51, NR21, NR22, NR23, NR24
	static const uint8_t square[] = { 0x3E, 0x80, 0xE0, 0x26, 0x3E, 0x77, 0xE0, 0x24, 0x3E, 0xFF, 0xE0, 0x25,
		                              0x3E, 0x80, 0xE0, 0x16, 0x3E, 0xF0, 0xE0, 0x17, 0x3E, 0x00, 0xE0, 0x18,
		                              0x3E, 0x87, 0xE0, 0x19 };
	size_t at = 0x150;
	if (audible) {
		memcpy(&rom[at], square, sizeof(square));
		at += sizeof(square);
	}
	rom[at] = 0x18; rom[at + 1] = 0xFE; // jr -2
	rom[at + 2] = 0x76;                 // halt
	static const uint8_t engine[] = { 0x21, 0x00, 0xD0, 0x34, 0xC9 };
	memcpy(&rom[GB_STUB_ENGINE], engine, sizeof(engine));
	rom[GB_STUB_SPIN] = 0x18; rom[GB_STUB_SPIN + 1] = 0xFE;
	// ld hl,$D000; inc [hl]; bit 0,[hl]; ret z; ld b,0; dec b; jr nz,-3; ret
	static const uint8_t slow[] = { 0x21, 0x00, 0xD0, 0x34, 0xCB, 0x46, 0xC8, 0x06, 0x00, 0x05, 0x20, 0xFD, 0xC9 };
	memcpy(&rom[GB_STUB_SLOW], slow, sizeof(slow));
}

#endif
