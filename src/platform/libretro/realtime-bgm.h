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

// Thumb entry points of the game's own m4a driver.
struct RealtimeBgmM4a {
	uint32_t soundMain;
	uint32_t soundVSync;
	uint32_t mplayStart;
	uint32_t mplayStop;
};

enum retro_bgm_status RealtimeBgmLocate(const uint8_t* rom, size_t romSize, struct RealtimeBgmM4a* out);

typedef uint8_t (*RealtimeBgmRead8)(void* ctx, uint32_t address);

// m4a has no music flag: music loops, has many tracks, or runs long.
bool RealtimeBgmSongIsMusic(RealtimeBgmRead8 read8, void* ctx, uint32_t songHeader);

struct mCore;
struct mAudioBuffer;
struct RealtimeBgmEngine;

// The m4a adapter; `rom` must outlive it.
bool RealtimeBgmM4aEngine(struct mCore* main, const void* rom, size_t romSize, const struct RealtimeBgmM4a* m4a,
                          struct RealtimeBgmEngine* out);

// Two parked shadow cores cloned from main: music at 1x wall time, effects in
// lockstep with main. Emulation thread only.
struct RealtimeBgm;

// The shadows map `rom` without copying it: destroy this before freeing it.
// Takes ownership of `engine->state`, failure included.
struct RealtimeBgm* RealtimeBgmCreate(struct mCore* main, const void* rom, size_t romSize,
                                      const struct RealtimeBgmEngine* engine, unsigned outRate);
void RealtimeBgmDestroy(struct RealtimeBgm*);

void RealtimeBgmAfterMainFrame(struct RealtimeBgm*);
void RealtimeBgmSetSpeed(struct RealtimeBgm*, double speed, bool enabled);
void RealtimeBgmRun(struct RealtimeBgm*, unsigned frames);
size_t RealtimeBgmRead(struct RealtimeBgm*, int16_t* out, size_t maxFrames);
// Main, the effects shadow, silence, or the hand-back mix.
struct mAudioBuffer* RealtimeBgmGameAudio(struct RealtimeBgm*, struct mAudioBuffer* mainBuffer);
bool RealtimeBgmActive(const struct RealtimeBgm*);
enum retro_bgm_status RealtimeBgmStatus(const struct RealtimeBgm*);
void RealtimeBgmMainJumped(struct RealtimeBgm*);
// The music lane's output rate, when the content's changes (GB).
void RealtimeBgmSetOutRate(struct RealtimeBgm*, unsigned outRate);

// Process-wide, so tests that only drive libretro.c can read them.
unsigned RealtimeBgmClonesForTesting(void);
unsigned RealtimeBgmLiveShadowsForTesting(void);
struct mCore* RealtimeBgmShadowForTesting(struct RealtimeBgm*, bool sfx);
void RealtimeBgmSetLocateOverrideForTesting(const struct RealtimeBgmM4a* m4a);

CXX_GUARD_END

#endif
