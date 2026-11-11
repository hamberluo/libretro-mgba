/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_GXA_H
#define GBA_GXA_H

#include <mgba-util/common.h>

CXX_GUARD_START

// GXA: external audio for GBA ROMs (gxa/SPEC.md). A ROM writes registers at
// 0x04FFFA00; the emulator plays tracks from a .gxa pack (a store-only zip).

#define GXA_BASE 0xFFFA00
#define GXA_SIZE 0x80
#define GXA_CHANNELS 4

struct VFile;
struct GXAPack;

enum GXAMatch {
	GXA_MATCH_NONE = 0,
	GXA_MATCH_SHA1,
	GXA_MATCH_HEADER,
};

// Takes ownership of vf; NULL on a bad zip, a compressed entry, a bad manifest or spec != 1.
struct GXAPack* GXAPackOpen(struct VFile* vf);
void GXAPackClose(struct GXAPack*);
enum GXAMatch GXAPackMatch(const struct GXAPack*, const void* rom, size_t size);
// False when the id is not listed or its file is not in the zip.
bool GXAPackTrack(const struct GXAPack*, uint16_t id, const void** data, size_t* size, int32_t* loop, float* gainDb);
void GXAPackId(const struct GXAPack*, uint8_t out[20]);
const char* GXAPackTitle(const struct GXAPack*);
const char* GXAPackVersion(const struct GXAPack*);

struct GXA;

struct GXA* GXACreate(void);
void GXADestroy(struct GXA*);
// Takes ownership; NULL unloads. Every channel stops and reads ended.
void GXASetPack(struct GXA*, struct GXAPack*);
struct GXAPack* GXAGetPack(const struct GXA*);
uint16_t GXARead16(struct GXA*, uint32_t offset);
void GXAWrite16(struct GXA*, uint32_t offset, uint16_t value);
// Q8 (256 = unity): GAMEVOL, times DUCK while any channel plays.
int32_t GXAGameGain(const struct GXA*);

CXX_GUARD_END

#endif
