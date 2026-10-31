/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef RETRO_AUX_CORE_H
#define RETRO_AUX_CORE_H

#include <mgba-util/common.h>

CXX_GUARD_START

// Cores the frontend never sees: link partners and realtime BGM shadows.
// One copy of their setup and teardown so the two cannot drift apart.

struct mCore;

// Anything that changes emulated results is pinned here instead of read from
// the frontend, so every aux core agrees whatever the settings say.
void RetroAuxCoreConfigure(struct mCore* core);
void RetroAuxCoreNeverDraw(struct mCore* core);
// Frees what mCoreInitConfig and init allocated. NULL is a no-op.
void RetroAuxCoreDestroy(struct mCore* core);

CXX_GUARD_END

#endif
