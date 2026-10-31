/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_BGM_H
#define LIBRETRO_BGM_H

// Realtime BGM extensions to the libretro API: at fast-forward, music keeps 1x
// tempo while sound effects follow the game. Frontends resolve them with dlsym;
// they are absent from other cores.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

enum retro_bgm_status {
	RETRO_BGM_NO_GAME = -1,
	RETRO_BGM_OK = 0,
	RETRO_BGM_NOT_GBA = 1,
	RETRO_BGM_NOT_M4A = 2,
	RETRO_BGM_LOCATE_VSYNC = 3,
	RETRO_BGM_LOCATE_MPLAY_START = 4,
	RETRO_BGM_LOCATE_MPLAY_STOP = 5,
	RETRO_BGM_CHAIN_INVALID = 6, // fell back this session
	RETRO_BGM_BUDGET = 7,        // fell back this session
};

// Above this speed only music plays: compressed sound effects are noise.
// Mirrors GoGBA's SpeedTierLimits.forcedMuteAbove.
#define RETRO_BGM_SFX_SPEED_MAX 8

// One of enum retro_bgm_status for the loaded game. Safe from any thread.
RETRO_API int retro_bgm_status(void);

// The rest run on the emulation thread, serial with retro_run.
// Target speed of the main core; above 1 with `enabled` the shadows take over,
// otherwise they hand back.
RETRO_API void retro_bgm_set_speed(double speed, bool enabled);
// Advances the music shadow by `frames` 1x frames of wall time.
RETRO_API void retro_bgm_run(unsigned frames);
// Drains the music shadow: interleaved stereo at the core's output rate.
RETRO_API size_t retro_bgm_read(int16_t* out, size_t max_frames);
// True while the shadows own the audio (taking over or handing back).
RETRO_API bool retro_bgm_active(void);

#ifdef __cplusplus
}
#endif

#endif
