/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_GXA_H
#define LIBRETRO_GXA_H

// GXA external audio (gxa/SPEC.md): a GBA ROM plays tracks from a .gxa pack.
// Frontends resolve these with dlsym; other cores lack them. Without a call to
// retro_gxa_load, retro_load_game loads <rom path without extension>.gxa when
// it exists.
//
// Only the main core has GXA. Aux cores (link partners, realtime BGM's
// shadows, which run only injected calls into the game's sound driver) fail
// the handshake: a sound driver that drives GXA itself would not be heard
// while realtime BGM takes over a fast-forward.

#include <stdbool.h>
#include <stddef.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

enum retro_gxa_status {
	RETRO_GXA_NONE = 0,
	RETRO_GXA_OK = 1,          // matched by the ROM's SHA-1, or by its header and a version the pack names
	RETRO_GXA_HEADER_ONLY = 2, // matched by game code and title only (no version, or one outside the pack's)
	RETRO_GXA_NO_MATCH = 3,
	RETRO_GXA_BAD_PACK = 4,
	RETRO_GXA_NOT_GBA = 5,
};

// Emulation thread, after retro_load_game. Replaces any pack loaded before;
// on NO_MATCH, BAD_PACK and NOT_GBA no pack stays attached.
RETRO_API int retro_gxa_load(const char* path);
RETRO_API void retro_gxa_unload(void);
// Not audible (muted fast-forward): channels still advance and end, nothing is decoded.
RETRO_API void retro_gxa_set_audible(bool audible);
// 0.0-1.0, GXA's output only.
RETRO_API void retro_gxa_set_volume(float volume);
// The last load's status. Safe from any thread.
RETRO_API int retro_gxa_status(void);
RETRO_API bool retro_gxa_pack_info(char* title, size_t titleSize, char* version, size_t versionSize);

#ifdef __cplusplus
}
#endif

#endif
