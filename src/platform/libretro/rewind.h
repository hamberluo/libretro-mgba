/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef RETRO_REWIND_INTERNAL_H
#define RETRO_REWIND_INTERNAL_H

#include <mgba-util/common.h>

CXX_GUARD_START

struct mCore;
struct VFile;

// One full savestate per 60 emulated frames, the newest `capacity` kept.
// Whole states rather than mGBA's per-frame deltas: at one per second a ring
// of 30 GBA states is ~11 MB, and a capture costs ~10 us on a host.
struct RetroRewind {
	struct VFile** slots;
	unsigned capacity;
	unsigned head; // next slot written
	unsigned count;
	unsigned frames; // since the last capture
};

// 0 frees everything. Either way the ring starts empty.
void RetroRewindConfigure(struct RetroRewind*, unsigned capacity);
void RetroRewindClear(struct RetroRewind*);
// Call once after every emulated frame.
void RetroRewindFrame(struct RetroRewind*, struct mCore*);
// Loads the snapshot `seconds` back (the oldest if fewer are kept) and drops
// it with every newer one. Returns how many seconds that was.
unsigned RetroRewindStep(struct RetroRewind*, struct mCore*, unsigned seconds);

CXX_GUARD_END

#endif
