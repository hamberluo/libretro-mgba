/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef REALTIME_BGM_H
#define REALTIME_BGM_H

#include <mgba-util/common.h>

#include "libretro_bgm.h"

CXX_GUARD_START

// The game's own m4a (Sappy/MP2K) entry points, as Thumb addresses (bit 0 clear).
struct RealtimeBgmM4a {
	uint32_t soundMain;
	uint32_t soundVSync;
	uint32_t mplayStart;
	uint32_t mplayStop;
};

// Static scan of a GBA ROM image. Matches what the code does, not fixed bytes,
// so agbcc, gcc and C-written drivers all resolve.
enum retro_bgm_status RealtimeBgmLocate(const uint8_t* rom, size_t romSize, struct RealtimeBgmM4a* out);

CXX_GUARD_END

#endif
