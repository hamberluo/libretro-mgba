/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef RETRO_LINK_INTERNAL_H
#define RETRO_LINK_INTERNAL_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/core/core.h>

// Link mode: every player's GBA / GB runs in this process, joined by a link
// cable, so a network only has to carry joypad input. Everything here is
// deterministic: the same ROM, saves, epoch and input produce the same state
// on any device.

// GBA links up to four (the multiplayer cable's limit); GB links two.
#define RETRO_LINK_MAX_PLAYERS 4

// One player's cartridge. Players may hold different ROMs of one platform,
// such as Pokémon Ruby and Sapphire, or a GB and a GBC game.
struct RetroLinkCart {
	const void* rom;
	size_t romSize;
	void* save;  // battery save buffer; the core reads and writes it in place
	size_t saveSize;
};

struct RetroLink;

// Cold-boots `players` cores of `platform`, one per cart, joined by a cable.
// The carts and `localVideo` are used in place: the local cart must outlive
// the local core even after RetroLinkEnd, the others until RetroLinkEnd.
// `localVideo` is needed now: a core binds its renderer at reset only if it
// has a buffer. NULL on bad arguments or a ROM that is not for `platform`.
struct RetroLink* RetroLinkCreate(enum mPlatform platform, const struct RetroLinkCart* carts,
                                  unsigned players, unsigned localPlayer, int64_t rtcEpochMs,
                                  mColor* localVideo, size_t localStride);

struct mCore* RetroLinkCore(struct RetroLink*, unsigned player);
// The core that draws into `localVideo` and plays sound.
struct mCore* RetroLinkLocalCore(struct RetroLink*);

// One libretro joypad mask (RETRO_DEVICE_ID_JOYPAD_* bits) per player, for the
// next frame. X / Y / L2 / R2 are turbo A / B / L / R, as in single player.
void RetroLinkSetInput(struct RetroLink*, const uint16_t* joypadMasks);

// Advances every core by one frame of emulated time. False means every core
// was blocked on the cable, which only a coordinator bug can cause.
bool RetroLinkRunFrame(struct RetroLink*);

// CRC32 over every core's RAM and registers, for two devices to compare.
uint32_t RetroLinkChecksum(struct RetroLink*);

// Unplugs the cable, frees every core but the local one and the link itself,
// and returns the local core, still running as a single-player game.
struct mCore* RetroLinkEnd(struct RetroLink*);

CXX_GUARD_END

#endif
