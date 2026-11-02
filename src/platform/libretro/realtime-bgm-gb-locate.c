/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "realtime-bgm-gb.h"

#include <string.h>

#define BANK_SIZE 0x4000
#define GEN2_HEAD_SIZE 15    // ld a,[wMusicPlaying] .. ld bc,wChannel1
#define GEN2_SCAN 0x400      // where the channel loop's stride is found
#define CHANNEL_LENGTH_MAX 0x40
#define GEN1_SIG_SIZE 10
#define WRAM_BASE 0xC000
#define WRAM_END 0xE000 // echo RAM from here
#define BANKS 256       // bank numbers are 8-bit

static bool override;
static struct RealtimeBgmGb overrideGb;

void RealtimeBgmGbSetLocateOverrideForTesting(const struct RealtimeBgmGb* gb) {
	override = gb != NULL;
	if (gb) {
		overrideGb = *gb;
	}
}

static uint16_t le16(const uint8_t* p) {
	return p[0] | p[1] << 8;
}

// `21 n 00 09 <tail>`: ld hl,n; add hl,bc; then the tail. Returns n, or -1.
static int ldHlAddBc(const uint8_t* p, size_t avail, uint8_t t0, uint8_t t1) {
	if (avail < 6 || p[0] != 0x21 || p[2] != 0x00 || p[3] != 0x09 || p[4] != t0 || p[5] != t1) {
		return -1;
	}
	return p[1];
}

// `ld a,[n]; bit b,a` anywhere in the engine's bank on a WRAM byte outside
// the copied block: the options byte (stereo). 0 if none, or if they disagree.
static uint16_t gen2Options(const uint8_t* rom, size_t romSize, size_t bank, uint16_t from, uint16_t to) {
	uint16_t found = 0;
	size_t end = (bank + 1) * BANK_SIZE < romSize ? (bank + 1) * BANK_SIZE : romSize;
	for (size_t a = bank * BANK_SIZE; a + 5 <= end; ++a) {
		const uint8_t* p = rom + a;
		uint16_t n = le16(p + 1);
		if (p[0] != 0xFA || p[3] != 0xCB || (p[4] & 0xC7) != 0x47 || n < WRAM_BASE || n >= WRAM_END ||
		    (n >= from && n < to)) {
			continue;
		}
		if (found && found != n) {
			return 0;
		}
		found = n;
	}
	return found;
}

// _UpdateSound: ld a,[wMusicPlaying]; and a; ret z; xor a; ld [..],a; ld [..],a;
// ld bc,wChannel1, with wChannel1 right after wMusicPlaying. The channel struct
// layout is read from the code that walks it: hacks move and resize it.
static bool gen2At(const uint8_t* rom, size_t romSize, size_t o, struct RealtimeBgmGb* out) {
	const uint8_t* p = rom + o;
	if (p[0] != 0xFA || p[3] != 0xA7 || p[4] != 0xC8 || p[5] != 0xAF || p[6] != 0xEA || p[9] != 0xEA || p[12] != 0x01) {
		return false;
	}
	uint16_t mp = le16(p + 1);
	if (mp < WRAM_BASE || le16(p + 13) != mp + 1) {
		return false;
	}
	size_t end = o + GEN2_SCAN < romSize ? o + GEN2_SCAN : romSize;
	int flags = ldHlAddBc(p + GEN2_HEAD_SIZE, end - o - GEN2_HEAD_SIZE, 0xCB, 0x46); // bit 0,[hl]
	int length = -1;
	for (size_t a = o + GEN2_HEAD_SIZE; a < end && length < 0; ++a) {
		length = ldHlAddBc(rom + a, end - a, 0x4D, 0x44); // ld c,l; ld b,h
	}
	// The shadows copy every channel and the globals after them.
	if (flags < 0 || length <= 0 || length > CHANNEL_LENGTH_MAX || flags >= length ||
	    mp + 1 + 8 * length + REALTIME_BGM_GB2_GLOBALS > WRAM_END) {
		return false;
	}
	uint16_t blockEnd = mp + 1 + 8 * length + REALTIME_BGM_GB2_GLOBALS;
	*out = (struct RealtimeBgmGb) {
		.family = REALTIME_BGM_GB_GEN2,
		.updateBank = o / BANK_SIZE,
		.updateSound = BANK_SIZE + o % BANK_SIZE,
		.musicPlaying = mp,
		.channelLength = length,
		.channelFlags = flags,
		.options = gen2Options(rom, romSize, o / BANK_SIZE, mp, blockEnd),
	};
	return true;
}

// UpdateMusic, one copy per audio bank: ld c,0; ld b,0; ld hl,wChannelSoundIDs;
// add hl,bc; ld a,[hl]; and a.
static bool gen1(const uint8_t* rom, size_t romSize, struct RealtimeBgmGb* out) {
	static const uint8_t head[] = { 0x0E, 0x00, 0x06, 0x00, 0x21 };
	static const uint8_t tail[] = { 0x09, 0x7E, 0xA7 };
	memset(out, 0, sizeof(*out));
	for (size_t o = BANK_SIZE; o + GEN1_SIG_SIZE <= romSize && o / BANK_SIZE < BANKS; ++o) {
		if (memcmp(rom + o, head, sizeof(head)) || memcmp(rom + o + 7, tail, sizeof(tail))) {
			continue;
		}
		uint16_t ids = le16(rom + o + 5);
		// The shadows copy C000-C0FF and keep wAudioROMBank: the table must fit before it.
		if (ids < WRAM_BASE || ids + 8 > REALTIME_BGM_GB1_AUDIO_BANK) {
			return false;
		}
		if (out->family && ids != out->soundIds) {
			return false;
		}
		out->family = REALTIME_BGM_GB_GEN1;
		out->soundIds = ids;
		out->updateMusic[o / BANK_SIZE] = BANK_SIZE + o % BANK_SIZE;
	}
	return out->family != 0;
}

enum retro_bgm_status RealtimeBgmGbLocate(const uint8_t* rom, size_t romSize, struct RealtimeBgmGb* out) {
	if (override) {
		*out = overrideGb;
		return RETRO_BGM_OK;
	}
	for (size_t o = BANK_SIZE; o + GEN2_HEAD_SIZE <= romSize && o / BANK_SIZE < BANKS; ++o) {
		if (gen2At(rom, romSize, o, out)) {
			return RETRO_BGM_OK;
		}
	}
	return gen1(rom, romSize, out) ? RETRO_BGM_OK : RETRO_BGM_GB_UNSUPPORTED;
}
