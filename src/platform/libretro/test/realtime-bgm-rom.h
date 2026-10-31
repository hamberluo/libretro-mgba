/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef REALTIME_BGM_TEST_ROM_H
#define REALTIME_BGM_TEST_ROM_H

// A cartridge that spins in `b .` and carries Thumb stubs standing in for the
// m4a driver, plus one looping song. Tests write the driver's RAM state (the
// SoundInfo, one player) themselves, as m4aSoundInit and a song start would.
// Larger than EWRAM so mGBA maps it as a cartridge, not a multiboot image.

#include <stdint.h>
#include <string.h>

#include "../realtime-bgm.h"

#define BGM_ROM_SIZE 0x80000
#define BGM_STUB_RETURN 0x08000100 // bx lr
#define BGM_STUB_SPIN 0x08000108   // b .
#define BGM_SONG 0x08000200        // 1 track: note, GOTO back, FINE

#define BGM_SOUND_INFO 0x03001000
#define BGM_PLAYER 0x03001400
#define BGM_ID 0x68736D53

static inline void bgmBuildRom(uint8_t* rom) {
	memset(rom, 0, BGM_ROM_SIZE);
	const uint32_t entry = 0xEA00002E; // b 0xC0
	const uint32_t spin = 0xEAFFFFFE;  // b .
	memcpy(&rom[0], &entry, 4);
	rom[0xB2] = 0x96;
	memcpy(&rom[0xC0], &spin, 4);
	rom[0x100] = 0x70; rom[0x101] = 0x47; // bx lr
	rom[0x108] = 0xFE; rom[0x109] = 0xE7; // b .
	const uint32_t track = BGM_SONG + 0x10;
	rom[0x200] = 1;
	memcpy(&rom[0x208], &track, 4);
	rom[0x210] = 0xD0; rom[0x211] = 0x3C; // note
	rom[0x212] = 0xB2;                    // GOTO track
	memcpy(&rom[0x213], &track, 4);
	rom[0x217] = 0xB1;                    // FINE
}

static inline struct RealtimeBgmM4a bgmStubs(void) {
	struct RealtimeBgmM4a m = { BGM_STUB_RETURN, BGM_STUB_RETURN, BGM_STUB_RETURN, BGM_STUB_RETURN };
	return m;
}

#endif
