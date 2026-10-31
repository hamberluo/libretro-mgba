/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "aux-core.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#ifdef M_CORE_GBA
#include <mgba/internal/gba/gba.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/gb.h>
#endif

#include <limits.h>

void RetroAuxCoreConfigure(struct mCore* core) {
	struct mCoreOptions opts = {
		.useBios = false,
		.skipBios = true,
		.volume = 0x100,
	};
	mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
	mCoreConfigSetDefaultIntValue(&core->config, "allowOpposingDirections", 0);
	mCoreConfigSetDefaultIntValue(&core->config, "sgb.borders", 0);
	mCoreConfigLoadDefaults(&core->config, &opts);
	mCoreLoadConfig(core);
}

void RetroAuxCoreNeverDraw(struct mCore* core) {
	switch (core->platform(core)) {
#ifdef M_CORE_GBA
	case mPLATFORM_GBA:
		((struct GBA*) core->board)->video.frameskip = INT_MAX;
		((struct GBA*) core->board)->video.frameskipCounter = INT_MAX;
		break;
#endif
#ifdef M_CORE_GB
	case mPLATFORM_GB:
		((struct GB*) core->board)->video.frameskip = INT_MAX;
		((struct GB*) core->board)->video.frameskipCounter = INT_MAX;
		break;
#endif
	default:
		break;
	}
}

void RetroAuxCoreDestroy(struct mCore* core) {
	if (!core) {
		return;
	}
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}
