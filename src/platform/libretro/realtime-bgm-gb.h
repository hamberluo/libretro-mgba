/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef REALTIME_BGM_GB_H
#define REALTIME_BGM_GB_H

#include <mgba-util/common.h>

#include "libretro_bgm.h"

CXX_GUARD_START

// Pokémon's own GB sound engines. Music and effects are the engine's own
// channel split: 1-4 music, 5-8 effects.
enum RealtimeBgmGbFamily { REALTIME_BGM_GB_GEN1 = 1, REALTIME_BGM_GB_GEN2 = 2 };

struct RealtimeBgmGb {
	enum RealtimeBgmGbFamily family;
	uint8_t updateBank;      // gen2: bank of _UpdateSound
	uint16_t updateSound;    // gen2: _UpdateSound, 0x4000-0x7FFF
	uint16_t musicPlaying;   // gen2: wMusicPlaying; wChannel1 = musicPlaying + 1
	uint8_t channelLength;   // gen2: 0x32 (Polished 0x24)
	uint8_t channelFlags;    // gen2: on flag offset, bit 0 (Polished 7)
	uint16_t soundIds;       // gen1: wChannelSoundIDs
	uint16_t updateMusic[256]; // gen1: UpdateMusic per ROM bank, 0 = none
};

#define REALTIME_BGM_GB1_AUDIO_BANK 0xC0EFu // gen1 wAudioROMBank
#define REALTIME_BGM_GB2_GLOBALS 0x40       // gen2 engine state after wChannel8, copied with the channels
#define REALTIME_BGM_GB1_DRUM_MAX 0x13      // gen1 channel 8 ids 01-13: the music's drums

struct mCore;
struct RealtimeBgmEngine;

// The Pokémon GB adapter for realtime-bgm.c.
bool RealtimeBgmGbEngine(struct mCore* main, const struct RealtimeBgmGb* gb, struct RealtimeBgmEngine* out);

enum retro_bgm_status RealtimeBgmGbLocate(const uint8_t* rom, size_t romSize, struct RealtimeBgmGb* out);
void RealtimeBgmGbSetLocateOverrideForTesting(const struct RealtimeBgmGb* gb);

CXX_GUARD_END

#endif
