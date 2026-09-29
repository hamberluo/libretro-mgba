/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_REWIND_H
#define LIBRETRO_REWIND_H

// Rewind extensions to the libretro API. Frontends resolve them with dlsym;
// they are absent from other cores.

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

// Keeps one snapshot per emulated second, the newest `seconds` of them; 0
// stops and frees. The ring starts empty, and is emptied again by
// retro_reset, retro_unserialize and retro_link_begin, and freed by
// retro_unload_game. Nothing is captured while linked.
RETRO_API void retro_rewind_configure(unsigned seconds);

// Restores the snapshot `seconds` back from now (the oldest if fewer are
// kept) and drops every newer one. Returns the seconds rewound: 0 when
// nothing is kept, no game is loaded, or while linked.
RETRO_API unsigned retro_rewind_step(unsigned seconds);

#ifdef __cplusplus
}
#endif

#endif
