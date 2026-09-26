/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LINK_TEST_ROM_H
#define LINK_TEST_ROM_H

// Tiny hand-assembled ROMs that keep a link cable busy. No commercial ROM can
// live in the repo, and a `b .` loop has no link traffic to be
// nondeterministic about.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The ROM is small enough that the core runs it as a multiboot image, copied
// to the start of EWRAM, so the results live well past it.
#define LINK_GBA_ACC 0x02030000     // running mix of every word received
#define LINK_GBA_COUNT 0x02030004   // loop iterations
#define LINK_GBA_WORD0 0x02030008   // latest SIOMULTI0 (parent's KEYINPUT)
#define LINK_GBA_WORD1 0x0203000A   // latest SIOMULTI1 (child's KEYINPUT)
#define LINK_GBA_ROM_SIZE 0x400
// Backdrop colour the ROM paints, BGR555. Not white: a renderer reset clears
// its buffer to white, so a white frame does not prove the core drew it.
#define LINK_GBA_BACKDROP 0x03E0

// Multiplayer-mode SIO: each iteration every GBA sends its KEYINPUT, the
// parent starts a transfer, and both fold what they received into EWRAM.
static inline void linkBuildGbaRom(uint8_t rom[LINK_GBA_ROM_SIZE]) {
	enum { BASE = 0xC0 };
	static const uint32_t pool[] = { 0x04000100, LINK_GBA_ACC, 0x2003, 0x05000000, LINK_GBA_BACKDROP, 0x04000000 };
	uint32_t code[48];
	int lit[6][2]; // { instruction index, pool index } for each literal load
	int nlit = 0;
	int n = 0;
	int loop, wait, bne;
	memset(rom, 0, LINK_GBA_ROM_SIZE);
#define LDRH(rd, rn, off) (0xE1D000B0 | (rn) << 16 | (rd) << 12 | ((off) & 0xF0) << 4 | ((off) & 0xF))
#define STRH(rd, rn, off) (0xE1C000B0 | (rn) << 16 | (rd) << 12 | ((off) & 0xF0) << 4 | ((off) & 0xF))
#define LDR_LIT(rd, idx) lit[nlit][0] = n; lit[nlit++][1] = (idx); code[n++] = 0xE59F0000 | (rd) << 12
#define BRANCH(cond, from, to) ((cond) << 28 | 0x0A000000 | (((to) - ((from) + 2)) & 0xFFFFFF))
	LDR_LIT(0, 3);                 // r0 = palette RAM
	LDR_LIT(1, 4);                 // r1 = LINK_GBA_BACKDROP
	code[n++] = STRH(1, 0, 0);     // backdrop colour, so a drawn frame is recognisable
	LDR_LIT(0, 5);                 // r0 = 0x04000000
	code[n++] = 0xE3A01000;        // mov r1, #0
	code[n++] = STRH(1, 0, 0);     // DISPCNT = 0: mode 0, no layers, just the backdrop
	LDR_LIT(5, 0);                 // r5 = 0x04000100
	LDR_LIT(6, 1);                 // r6 = LINK_GBA_ACC
	code[n++] = 0xE3A00000;        // mov r0, #0
	code[n++] = STRH(0, 5, 0x34);  // RCNT = 0: serial mode
	LDR_LIT(0, 2);                 // r0 = 0x2003
	code[n++] = STRH(0, 5, 0x28);  // SIOCNT: multiplayer, 115200 bps
	code[n++] = 0xE3A07000;        // mov r7, #0
	loop = n;
	code[n++] = LDRH(0, 5, 0x30);  // r0 = KEYINPUT
	code[n++] = STRH(0, 5, 0x2A);  // SIOMLT_SEND = r0
	code[n++] = LDRH(1, 5, 0x28);  // r1 = SIOCNT
	code[n++] = 0xE3110004;        // tst r1, #4 (SI set: child)
	bne = n++;                     // bne wait (patched below)
	code[n++] = 0xE3811080;        // orr r1, r1, #0x80
	code[n++] = STRH(1, 5, 0x28);  // parent starts the transfer
	wait = n;
	code[n++] = LDRH(1, 5, 0x28);  // wait: r1 = SIOCNT
	code[n++] = 0xE3110080;        // tst r1, #0x80 (busy)
	code[n] = BRANCH(0x1, n, wait); ++n; // bne wait
	code[bne] = BRANCH(0x1, bne, wait);
	code[n++] = LDRH(2, 5, 0x20);  // r2 = SIOMULTI0
	code[n++] = LDRH(3, 5, 0x22);  // r3 = SIOMULTI1
	code[n++] = STRH(2, 6, 0x08);
	code[n++] = STRH(3, 6, 0x0A);
	code[n++] = 0xE0877002;        // add r7, r7, r2
	code[n++] = 0xE02771E3;        // eor r7, r7, r3, ror #3
	code[n++] = 0xE5867000;        // str r7, [r6]
	code[n++] = 0xE5960004;        // ldr r0, [r6, #4]
	code[n++] = 0xE2800001;        // add r0, r0, #1
	code[n++] = 0xE5860004;        // str r0, [r6, #4]
	code[n] = BRANCH(0xE, n, loop); ++n; // b loop
	int i;
	for (i = 0; i < nlit; ++i) {
		// The pool follows the code; PC reads 8 bytes ahead.
		code[lit[i][0]] |= (n + lit[i][1]) * 4 - (lit[i][0] * 4 + 8);
	}
#undef LDRH
#undef STRH
#undef LDR_LIT
#undef BRANCH
	// Entry `b BASE`: byte 3 == 0xEA and 0xB2 == 0x96 make it a GBA ROM.
	uint32_t entry = 0xEA000000 | ((BASE - 8) >> 2);
	memcpy(&rom[0], &entry, 4);
	rom[0xB2] = 0x96;
	memcpy(&rom[BASE], code, n * 4);
	memcpy(&rom[BASE + n * 4], pool, sizeof(pool));
}

// A GBA ROM that never touches the link port: `b .` after a valid entry.
static inline void linkBuildGbaIdleRom(uint8_t rom[LINK_GBA_ROM_SIZE]) {
	memset(rom, 0, LINK_GBA_ROM_SIZE);
	uint32_t entry = 0xEA00002E;   // b 0xC0
	uint32_t spin = 0xEAFFFFFE;    // b .
	memcpy(&rom[0], &entry, 4);
	rom[0xB2] = 0x96;
	memcpy(&rom[0xC0], &spin, 4);
}

#define LINK_GB_COUNT 0xC000   // transfers started
#define LINK_GB_RECV 0xC001    // last byte received
#define LINK_GB_SAVE0 0xC002   // first byte of cartridge RAM at boot
#define LINK_GB_ROM_SIZE 0x8000

// Serial transfer with internal clock, then a long idle loop (~16 frames)
// before the next one: the idle stretches are what let a lockstep slave that
// grants itself cycles bank time. Cartridge RAM is enabled and its first byte
// copied to WRAM, so a test can see which save each core booted with; the
// transfer counter is written back to cartridge RAM at 0xA001. With
// `doubleSpeed`, the ROM is CGB-only and switches to double speed first.
static inline void linkBuildGbRom(uint8_t rom[LINK_GB_ROM_SIZE], bool doubleSpeed) {
	static const uint8_t speedSwitch[] = {
		0x3E, 0x01, 0xE0, 0x4D, 0x10, 0x00, // ld a,1 ; ldh (4D),a ; stop
	};
	static const uint8_t code[] = {
		0x3E, 0x0A, 0xEA, 0x00, 0x00,       // ld a,0x0A ; ld (0000),a  RAM enable
		0xFA, 0x00, 0xA0, 0xEA, 0x02, 0xC0, // ld a,(A000) ; ld (C002),a
		0x3E, 0x00, 0xEA, 0x00, 0xC0,       // ld a,0 ; ld (C000),a
		0xFA, 0x00, 0xC0, 0x3C,             // loop: ld a,(C000) ; inc a
		0xEA, 0x00, 0xC0, 0xEA, 0x01, 0xA0, // ld (C000),a ; ld (A001),a
		0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02, // ldh (01),a ; ld a,0x81 ; ldh (02),a
		0xF0, 0x02, 0xCB, 0x7F, 0x20, 0xFA, // wait: ldh a,(02) ; bit 7,a ; jr nz,wait
		0xF0, 0x01, 0xEA, 0x01, 0xC0,       // ldh a,(01) ; ld (C001),a
		0x06, 0xFF,                         // ld b,0xFF
		0x0E, 0x00,                         // outer: ld c,0
		0x0D, 0x20, 0xFD,                   // inner: dec c ; jr nz,inner
		0x05, 0x20, 0xF8,                   // dec b ; jr nz,outer
		0x18, 0xD9,                         // jr loop
	};
	static const uint8_t entry[] = { 0x00, 0xC3, 0x50, 0x01 };     // nop ; jp 0x150
	static const uint8_t logoStart[] = { 0xCE, 0xED, 0x66, 0x66 }; // what GBIsROM checks
	size_t at = 0x150;
	memset(rom, 0, LINK_GB_ROM_SIZE);
	memcpy(&rom[0x100], entry, sizeof(entry));
	memcpy(&rom[0x104], logoStart, sizeof(logoStart));
	memcpy(&rom[0x134], "LINKTEST", 8);
	if (doubleSpeed) {
		rom[0x143] = 0xC0;  // CGB only
		memcpy(&rom[at], speedSwitch, sizeof(speedSwitch));
		at += sizeof(speedSwitch);
	}
	rom[0x147] = 0x03;  // MBC1 + RAM + battery
	rom[0x149] = 0x02;  // 8 KiB RAM
	memcpy(&rom[at], code, sizeof(code)); // jr offsets are relative, so the shift is harmless
	uint8_t check = 0;
	int i;
	for (i = 0x134; i < 0x14D; ++i) {
		check = check - rom[i] - 1;
	}
	rom[0x14D] = check;
}

// A GB that turns the LCD off, enables only the serial interrupt and halts,
// as Pokémon Crystal does while it waits on the cable at boot. Nothing ever
// wakes it: no transfer starts. A halted core keeps skipping to its next event
// inside one runLoop call, so only a frame deadline brings it back.
static inline void linkBuildGbHaltRom(uint8_t rom[LINK_GB_ROM_SIZE]) {
	static const uint8_t code[] = {
		0x3E, 0x00, 0xE0, 0x40, // ld a,0 ; ldh (40),a   LCD off
		0x3E, 0x08, 0xE0, 0xFF, // ld a,8 ; ldh (FF),a   IE = serial only
		0xFB,                   // ei
		0x76,                   // loop: halt
		0x18, 0xFD,             // jr loop
	};
	linkBuildGbRom(rom, false);
	memset(&rom[0x150], 0, 0x100);
	memcpy(&rom[0x150], code, sizeof(code));
}

#endif
