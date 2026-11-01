/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef REALTIME_BGM_ENGINE_H
#define REALTIME_BGM_ENGINE_H

#include <mgba-util/common.h>

#include <mgba/core/interface.h>

#include "libretro_bgm.h"

CXX_GUARD_START

struct mCore;

// What a sound driver adapter sees of a shadow; the rest is realtime-bgm.c's.
struct RealtimeBgmShadow {
	struct mCore* core;
	mColor* video;
	bool sfx;
};

// The driver-specific half of realtime BGM. realtime-bgm.c owns the state
// machine, the shadows' lifetime and the lanes; an engine knows the game's
// sound driver.
struct RealtimeBgmEngine {
	void* state; // owned: destroy() frees it
	/// True once the game's driver is up in main. False with *fail != RETRO_BGM_OK falls back.
	bool (*ready)(void* state, enum retro_bgm_status* fail);
	/// Just loaded main's state: park the CPU, release locks, silence what main left sounding.
	void (*cloned)(void* state, struct RealtimeBgmShadow*);
	/// One shadow frame: sync from main, call the driver, run the frame. False past the budget.
	bool (*frame)(void* state, struct RealtimeBgmShadow*);
	/// The hand-back may start fading.
	bool (*settled)(void* state, const struct RealtimeBgmShadow* bgm, const struct RealtimeBgmShadow* sfx);
	/// Every main frame, active or not.
	void (*observe)(void* state);
	void (*destroy)(void* state);
};

CXX_GUARD_END

#endif
