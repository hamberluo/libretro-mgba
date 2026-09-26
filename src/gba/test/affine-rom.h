/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef AFFINE_ROM_H
#define AFFINE_ROM_H

// A ROM that calls BgAffineSet and ObjAffineSet over fixed inputs and stores
// the outputs in EWRAM. Shared by bios-affine-emit (run on the real BIOS to
// produce the golden table) and bios-affine (run on the HLE to compare).

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>

#include <string.h>

#define AFFINE_BG_COUNT 1024
#define AFFINE_OBJ_COUNT 1024
#define AFFINE_BG_SRC 0x100
#define AFFINE_OBJ_SRC (AFFINE_BG_SRC + AFFINE_BG_COUNT * 20)
#define AFFINE_BG_DST 0x02000000
#define AFFINE_OBJ_DST 0x02008000
#define AFFINE_DONE 0x0203FFF0
#define AFFINE_OUT_HALFWORDS (AFFINE_BG_COUNT * 8 + AFFINE_OBJ_COUNT * 4)
#define AFFINE_ROM_WORDS 0x4000

// Four scale pairs per angle, including the extremes the float code got wrong.
static const int16_t affineScales[4][2] = {
	{ 0x100, 0x100 }, { 0x4000, 0x4000 }, { -0x180, 0x0A3 }, { 0x7FFF, -0x8000 },
};

static inline void affineBuildRom(uint32_t rom[AFFINE_ROM_WORDS]) {
	uint16_t* rom16 = (uint16_t*) rom;
	memset(rom, 0, AFFINE_ROM_WORDS * 4);
	// ldr r0-r2 from the pool, swi 0x0E; ldr r0-r2, mov r3 #2, swi 0x0F;
	// store 0x600D to AFFINE_DONE; b .
	static const uint32_t pool[] = {
		0x08000000 + AFFINE_BG_SRC, AFFINE_BG_DST, AFFINE_BG_COUNT,
		0x08000000 + AFFINE_OBJ_SRC, AFFINE_OBJ_DST, AFFINE_OBJ_COUNT,
		AFFINE_DONE, 0x600D,
	};
	uint32_t code[16];
	int n = 0;
#define LDR_POOL(rd, idx) code[n] = 0xE59F0000 | (rd) << 12 | ((0x80 + (idx) * 4) - (n * 4 + 8)); ++n
	LDR_POOL(0, 0); LDR_POOL(1, 1); LDR_POOL(2, 2); code[n++] = 0xEF0E0000;
	LDR_POOL(0, 3); LDR_POOL(1, 4); LDR_POOL(2, 5); code[n++] = 0xE3A03002; code[n++] = 0xEF0F0000;
	LDR_POOL(4, 6); LDR_POOL(5, 7); code[n++] = 0xE5845000; code[n++] = 0xEAFFFFFE;
#undef LDR_POOL
	memcpy(rom, code, n * 4);
	memcpy((uint8_t*) rom + 0x80, pool, sizeof(pool));
	int i;
	for (i = 0; i < AFFINE_BG_COUNT; ++i) {
		uint32_t o = (AFFINE_BG_SRC + i * 20) / 2;
		int32_t ox = (i * 7919) % 0x40000 - 0x20000;
		int32_t oy = (i * 104729) % 0x40000 - 0x20000;
		rom16[o] = ox; rom16[o + 1] = (uint32_t) ox >> 16;
		rom16[o + 2] = oy; rom16[o + 3] = (uint32_t) oy >> 16;
		rom16[o + 4] = (i * 37) % 480 - 240;
		rom16[o + 5] = (i * 53) % 320 - 160;
		rom16[o + 6] = affineScales[i >> 8][0];
		rom16[o + 7] = affineScales[i >> 8][1];
		rom16[o + 8] = (i & 0xFF) << 8 | (i & 0xFF) >> 1;
	}
	for (i = 0; i < AFFINE_OBJ_COUNT; ++i) {
		uint32_t o = (AFFINE_OBJ_SRC + i * 8) / 2;
		rom16[o] = affineScales[i >> 8][0];
		rom16[o + 1] = affineScales[i >> 8][1];
		rom16[o + 2] = (i & 0xFF) << 8 | 0x5A;
	}
}

// Runs the ROM on `core` (BIOS already loaded or not) and reads the outputs.
// Returns false if the ROM never reached its end marker.
static inline bool affineRun(struct mCore* core, uint32_t rom[AFFINE_ROM_WORDS], uint16_t out[AFFINE_OUT_HALFWORDS]) {
	struct GBA* gba = core->board;
	GBASkipBIOS(gba);
	long guard;
	for (guard = 0; guard < 1000000 && core->busRead32(core, AFFINE_DONE) != 0x600D; ++guard) {
		core->runLoop(core);
	}
	if (core->busRead32(core, AFFINE_DONE) != 0x600D) {
		return false;
	}
	int i;
	for (i = 0; i < AFFINE_BG_COUNT * 8; ++i) {
		out[i] = core->busRead16(core, AFFINE_BG_DST + i * 2);
	}
	for (i = 0; i < AFFINE_OBJ_COUNT * 4; ++i) {
		out[AFFINE_BG_COUNT * 8 + i] = core->busRead16(core, AFFINE_OBJ_DST + i * 2);
	}
	return true;
}

#endif
