/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "rewind.h"

#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>

#define REWIND_FRAMES_PER_SNAPSHOT 60

void RetroRewindConfigure(struct RetroRewind* rewind, unsigned capacity) {
	unsigned i;
	for (i = 0; i < rewind->capacity; ++i) {
		rewind->slots[i]->close(rewind->slots[i]);
	}
	free(rewind->slots);
	memset(rewind, 0, sizeof(*rewind));
	if (!capacity) {
		return;
	}
	struct VFile** slots = calloc(capacity, sizeof(*slots));
	if (!slots) {
		return;
	}
	for (i = 0; i < capacity; ++i) {
		slots[i] = VFileMemChunk(NULL, 0);
		if (!slots[i]) {
			while (i--) {
				slots[i]->close(slots[i]);
			}
			free(slots);
			return;
		}
	}
	rewind->slots = slots;
	rewind->capacity = capacity;
}

void RetroRewindClear(struct RetroRewind* rewind) {
	rewind->count = 0;
	rewind->frames = 0;
}

void RetroRewindFrame(struct RetroRewind* rewind, struct mCore* core) {
	if (!rewind->capacity || ++rewind->frames < REWIND_FRAMES_PER_SNAPSHOT) {
		return;
	}
	rewind->frames = 0;
	struct VFile* slot = rewind->slots[rewind->head];
	slot->truncate(slot, 0);
	slot->seek(slot, 0, SEEK_SET);
	// No SAVESTATE_SAVEDATA: retro_unserialize never restores the battery save
	// either, so a rewind must not roll it back.
	if (!mCoreSaveStateNamed(core, slot, SAVESTATE_RTC)) {
		// When the ring is full this slot held the oldest snapshot.
		if (rewind->count == rewind->capacity) {
			--rewind->count;
		}
		return;
	}
	rewind->head = (rewind->head + 1) % rewind->capacity;
	if (rewind->count < rewind->capacity) {
		++rewind->count;
	}
}

unsigned RetroRewindStep(struct RetroRewind* rewind, struct mCore* core, unsigned seconds) {
	if (!seconds || !rewind->count) {
		return 0;
	}
	// Seconds are counted from now: a newest snapshot under half a second old
	// is where the player already is, so the target is one further back.
	bool newestIsNow = rewind->frames < REWIND_FRAMES_PER_SNAPSHOT / 2;
	unsigned back = seconds + newestIsNow;
	if (back > rewind->count) {
		back = rewind->count;
	}
	unsigned rewound = back - newestIsNow;
	if (!rewound) {
		return 0;
	}
	unsigned target = (rewind->head + rewind->capacity - back) % rewind->capacity;
	struct VFile* slot = rewind->slots[target];
	slot->seek(slot, 0, SEEK_SET);
	if (!mCoreLoadStateNamed(core, slot, SAVESTATE_RTC)) {
		return 0;
	}
	// The loaded snapshot stays as the newest: it is exactly now.
	rewind->head = (target + 1) % rewind->capacity;
	rewind->count -= back - 1;
	rewind->frames = 0;
	return rewound;
}
