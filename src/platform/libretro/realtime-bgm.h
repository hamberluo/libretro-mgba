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

typedef uint8_t (*RealtimeBgmRead8)(void* ctx, uint32_t address);

// m4a has no music flag; a song loops, and so is music, when any track jumps
// back (GOTO) before its first FINE. Pointer arguments are skipped so their
// bytes are never read as commands.
bool RealtimeBgmSongLoops(RealtimeBgmRead8 read8, void* ctx, uint32_t songHeader);

struct mCore;
struct mAudioBuffer;

// Two shadow GBA cores cloned from `main` with their CPUs parked: one plays the
// music players at 1x wall time, the other everything else in lockstep with
// main. Only our calls into the game's own driver ever run on them. Every call
// is on the emulation thread, serial with the main core.
struct RealtimeBgm;

// `rom` is main's ROM buffer, mapped (not copied) by the shadows: destroy this
// before freeing it. `outRate` is the rate RealtimeBgmRead resamples to.
struct RealtimeBgm* RealtimeBgmCreate(struct mCore* main, const void* rom, size_t romSize,
                                      const struct RealtimeBgmM4a* m4a, unsigned outRate);
void RealtimeBgmDestroy(struct RealtimeBgm*);

// After every main frame, 1x included: learns which players are music, and
// while active syncs and advances the sound effects shadow.
void RealtimeBgmAfterMainFrame(struct RealtimeBgm*);
// Above 1 with `enabled` the shadows take over; otherwise they hand back.
void RealtimeBgmSetSpeed(struct RealtimeBgm*, double speed, bool enabled);
// Advances the music shadow by `frames` 1x frames of wall time.
void RealtimeBgmRun(struct RealtimeBgm*, unsigned frames);
// Interleaved stereo at outRate, hand-back fade applied.
size_t RealtimeBgmRead(struct RealtimeBgm*, int16_t* out, size_t maxFrames);
// What audio_batch plays in place of `mainBuffer`: main itself, the sound
// effects shadow, silence above RETRO_BGM_SFX_SPEED_MAX, or the hand-back mix.
struct mAudioBuffer* RealtimeBgmGameAudio(struct RealtimeBgm*, struct mAudioBuffer* mainBuffer);
// Taking over or handing back.
bool RealtimeBgmActive(const struct RealtimeBgm*);
enum retro_bgm_status RealtimeBgmStatus(const struct RealtimeBgm*);
// Main's state was replaced (load state, rewind, reset): reclone if active.
void RealtimeBgmMainJumped(struct RealtimeBgm*);

unsigned RealtimeBgmCloneCountForTesting(const struct RealtimeBgm*);
unsigned RealtimeBgmLiveShadowsForTesting(void);

CXX_GUARD_END

#endif
