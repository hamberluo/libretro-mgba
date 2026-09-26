/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_GOGBA_H
#define LIBRETRO_GOGBA_H

// GoGBA extensions to the libretro API. Frontends resolve them with dlsym;
// they are absent from other cores.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

struct retro_gogba_link_player {
	const void* save; // battery save bytes; NULL for none
	size_t save_size;
};

// Call after retro_load_game (and after writing the local save into
// RETRO_MEMORY_SAVE_RAM). Rebuilds every player's machine from the loaded ROM,
// loads saves[i] for player i -- saves[local_player] replaces what SAVE_RAM
// holds -- and cold-boots them all joined by a link cable. SAVE_RAM keeps its
// address and becomes the local player's save. Only the local player's machine
// is shown and heard. Returns false (and changes nothing) if a link is already
// running, the arguments are out of range, or no game is loaded.
RETRO_API bool retro_gogba_link_begin(unsigned players, unsigned local_player,
                                      const struct retro_gogba_link_player* saves,
                                      int64_t rtc_epoch_ms);

// Every player's RETRO_DEVICE_ID_JOYPAD_* mask for the next retro_run.
// While linked, retro_run ignores the input callback.
RETRO_API void retro_gogba_link_set_input(const uint16_t* joypad_masks);

// State digest to compare across devices; 0 when not linked.
RETRO_API uint32_t retro_gogba_link_checksum(void);

// Fingerprint of the emulation code; two devices may link only if equal.
RETRO_API uint64_t retro_gogba_link_core_id(void);

// Unplugs the cable; the local player's game keeps running single-player.
RETRO_API void retro_gogba_link_end(void);

#ifdef __cplusplus
}
#endif

#endif
