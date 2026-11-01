/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "realtime-bgm.h"

#include <string.h>

#define ROM_BASE 0x08000000u
#define SOUND_INFO_PTR 0x03007FF0u
#define M4A_ID 0x68736D53u

struct Scan {
	const uint8_t* rom;
	size_t size;
};

// Out-of-range reads give 0, a plain `movs r0, r0` that matches nothing below.
static uint16_t h16(const struct Scan* s, size_t off) {
	return off + 2 <= s->size ? (uint16_t) (s->rom[off] | s->rom[off + 1] << 8) : 0;
}

static uint32_t w32(const struct Scan* s, size_t off) {
	return (uint32_t) h16(s, off) | (uint32_t) h16(s, off + 2) << 16;
}

// Thumb `ldr rX, [pc, #imm]` at `off`: its literal, or -1.
static int64_t litAt(const struct Scan* s, size_t off) {
	uint16_t op = h16(s, off);
	if ((op >> 11) != 0x09) {
		return -1;
	}
	size_t lit = ((off + 4) & ~(size_t) 3) + (op & 0xFF) * 4;
	return lit + 4 <= s->size ? (int64_t) w32(s, lit) : -1;
}

static bool isTerminator(uint16_t op) {
	return (op & 0xFF87) == 0x4700 /* bx */ || (op & 0xFF00) == 0xBD00 /* pop {..,pc} */ || op == 0x46C0 /* nop pad */;
}

// Nearest preceding `push {..., lr}` within `range` bytes, extended over gcc's
// two-push prologue (`push {..,lr}; mov rX, r8-r11...; push {..}`). 0 if none.
static size_t pushAbove(const struct Scan* s, size_t off, size_t range) {
	size_t p = 0;
	bool found = false;
	for (size_t b = 0; b <= range && b <= off; b += 2) {
		if ((h16(s, off - b) & 0xFF00) == 0xB500) {
			p = off - b;
			found = true;
			break;
		}
	}
	if (!found) {
		return 0;
	}
	for (;;) {
		size_t q = p;
		while (q >= 2 && (h16(s, q - 2) & 0xFF40) == 0x4640) { // mov rX, r8-r15
			q -= 2;
		}
		if (q == p || q < 2 || (h16(s, q - 2) & 0xFF00) != 0xB500) {
			break;
		}
		p = q - 2;
	}
	return p;
}

#define MAX_CANDIDATES 9

// Function entry for the push at `push`: the push itself or an instruction just
// above it, when gcc scheduled loads ahead of the push. Candidates stop at the
// previous function's end; the one most BLs land on wins, so a data word that
// decodes as BL cannot pull the entry into the middle of a function. One pass
// over the ROM counts every candidate.
static size_t entryOf(const struct Scan* s, size_t push) {
	size_t cand[MAX_CANDIDATES];
	unsigned hits[MAX_CANDIDATES] = { 0 };
	size_t n = 0;
	cand[n++] = push;
	for (size_t c = push; c >= 2 && push - c < 16 && n < MAX_CANDIDATES; c -= 2) {
		if (isTerminator(h16(s, c - 2))) {
			break;
		}
		cand[n++] = c - 2;
	}
	for (size_t off = 0; off + 4 <= s->size; off += 2) {
		uint16_t hi = h16(s, off);
		if ((hi & 0xF800) != 0xF000) {
			continue;
		}
		uint16_t lo = h16(s, off + 2);
		if ((lo & 0xF800) != 0xF800) {
			continue;
		}
		int32_t o = ((int32_t) ((uint32_t) (hi & 0x7FF) << 21) >> 9) | ((lo & 0x7FF) << 1);
		int64_t target = (int64_t) off + 4 + o;
		for (size_t i = 0; i < n; ++i) {
			hits[i] += target == (int64_t) cand[i];
		}
	}
	size_t best = 0;
	for (size_t i = 1; i < n; ++i) {
		if (hits[i] > hits[best]) {
			best = i;
		}
	}
	return cand[best];
}

// SoundMain is m4a's hand-written assembly, identical in every version:
// ldr r0,=SOUND_INFO_PTR; ldr r0,[r0]; ldr r2,=ID; ldr r3,[r0]; cmp r2,r3; beq; bx lr; adds r3,#1; str r3,[r0]
static uint32_t findSoundMain(const struct Scan* s) {
	static const int16_t pat[] = { -1, 0x48, 0x00, 0x68, -1, 0x4A, 0x03, 0x68, 0x9A, 0x42, -1, 0xD0, 0x70, 0x47, 0x01, 0x33, 0x03, 0x60 };
	const size_t n = sizeof(pat) / sizeof(pat[0]);
	for (size_t off = 0; off + n <= s->size; off += 2) {
		size_t i;
		for (i = 0; i < n && (pat[i] < 0 || s->rom[off + i] == pat[i]); ++i)
			;
		if (i == n && litAt(s, off) == SOUND_INFO_PTR && litAt(s, off + 4) == M4A_ID) {
			return ROM_BASE + (uint32_t) off;
		}
	}
	return 0;
}

// SoundVSync: loads SOUND_INFO_PTR, checks the ident (cmp ID, or add -ID when
// written in C), then decrements the pcmDmaCounter byte at +4.
static uint32_t findVSync(const struct Scan* s) {
	for (size_t off = 0; off + 64 < s->size; off += 2) {
		if (litAt(s, off) != SOUND_INFO_PTR) {
			continue;
		}
		bool ident = false;
		for (size_t d = 2; d < 16; d += 2) {
			int64_t l = litAt(s, off + d);
			ident |= l == M4A_ID || l == (uint32_t) -M4A_ID;
		}
		if (!ident) {
			continue;
		}
		for (size_t d = 2; d < 40; d += 2) {
			uint16_t a = h16(s, off + d);
			if ((a & 0xFFC0) != 0x7900) { // ldrb rd,[rn,#4]
				continue;
			}
			int rd = a & 7, rn = (a >> 3) & 7;
			uint16_t b = h16(s, off + d + 2);
			if (b != (0x3800 | rd << 8 | 1) && b != (0x1E40 | rd << 3 | rd)) { // subs rd,#1
				continue;
			}
			for (size_t e = 4; e < 10; e += 2) {
				uint16_t c = h16(s, off + d + e);
				if ((c & 0xFFC0) == 0x7100 && ((c >> 3) & 7) == rn) { // strb _,[rn,#4]
					size_t push = pushAbove(s, off, 8);
					return ROM_BASE + (uint32_t) (push ? push : off);
				}
			}
		}
	}
	return 0;
}

// MPlayStart: the only function that resets tempoD to 150 (+0x1C), as a
// halfword (agbcc) or the merged word 0x01000096 (gcc), in a body that also
// writes the player's ident lock at +0x34.
static uint32_t findMPlayStart(const struct Scan* s) {
	for (size_t off = 0; off + 64 < s->size; off += 2) {
		uint16_t t = h16(s, off);
		int rt = (t >> 8) & 7;
		bool tempo = false;
		if ((t & 0xF8FF) == 0x2096) { // movs rt,#150 ... strh rt,[_,#0x1C]
			for (size_t e = 2; e < 16; e += 2) {
				tempo |= (h16(s, off + e) & 0xFFC7) == (0x8380 | rt);
			}
		} else if (litAt(s, off) == 0x01000096) { // ... str rt,[_,#0x1C]
			for (size_t e = 2; e < 16; e += 2) {
				tempo |= (h16(s, off + e) & 0xFFC7) == (0x6000 | 7 << 6 | rt);
			}
		}
		if (!tempo) {
			continue;
		}
		size_t push = pushAbove(s, off, 0xC0);
		if (!push) {
			continue;
		}
		bool lock = false;
		for (size_t o = push; o < off + 0x40; o += 2) {
			lock |= (h16(s, o) & 0xFFC0) == (0x6000 | 13 << 6); // str _,[_,#0x34]
		}
		if (lock) {
			return ROM_BASE + (uint32_t) entryOf(s, push);
		}
	}
	return 0;
}

// MPlayStop: sets the pause bit (0x80 << 24) in status (+4), then walks the
// tracks (stride 0x50) calling TrackStop. Several copies may exist (Kirby);
// the real one follows MPlayStart in the same compilation unit.
static uint32_t findMPlayStop(const struct Scan* s, uint32_t mplayStart) {
	size_t from = mplayStart - ROM_BASE;
	for (size_t off = from; off + 64 < s->size && off < from + 0x800; off += 2) {
		uint16_t a = h16(s, off);
		if ((a & 0xF8FF) != 0x2080) { // movs rx,#0x80
			continue;
		}
		int rx = (a >> 8) & 7;
		bool shifted = false;
		for (size_t d = 2; d <= 6; d += 2) {
			shifted |= h16(s, off + d) == (0x0600 | 24 << 6 | rx << 3 | rx); // lsls rx,rx,#24
		}
		if (!shifted) {
			continue;
		}
		bool status = false, loop = false, call = false;
		for (size_t d = 4; d < 20; d += 2) {
			status |= (h16(s, off + d) & 0xFFC0) == 0x6040; // str _,[_,#4]
		}
		for (size_t d = 4; d < 48; d += 2) {
			uint16_t c = h16(s, off + d);
			loop |= (c & 0xF8FF) == 0x3050; // adds ry,#0x50
			call |= (c & 0xF800) == 0xF000; // bl
		}
		if (!status || !loop || !call) {
			continue;
		}
		size_t push = pushAbove(s, off, 0x40);
		if (push && ROM_BASE + push > mplayStart) {
			return ROM_BASE + (uint32_t) entryOf(s, push);
		}
	}
	return 0;
}

static const struct RealtimeBgmM4a* locateOverride;

void RealtimeBgmSetLocateOverrideForTesting(const struct RealtimeBgmM4a* m4a) {
	locateOverride = m4a;
}

enum retro_bgm_status RealtimeBgmLocate(const uint8_t* rom, size_t romSize, struct RealtimeBgmM4a* out) {
	struct Scan s = { rom, romSize };
	if (locateOverride) {
		*out = *locateOverride;
		return RETRO_BGM_OK;
	}
	memset(out, 0, sizeof(*out));
	out->soundMain = findSoundMain(&s);
	if (!out->soundMain) {
		return RETRO_BGM_NOT_M4A;
	}
	out->soundVSync = findVSync(&s);
	if (!out->soundVSync) {
		return RETRO_BGM_LOCATE_VSYNC;
	}
	out->mplayStart = findMPlayStart(&s);
	if (!out->mplayStart) {
		return RETRO_BGM_LOCATE_MPLAY_START;
	}
	out->mplayStop = findMPlayStop(&s, out->mplayStart);
	if (!out->mplayStop) {
		return RETRO_BGM_LOCATE_MPLAY_STOP;
	}
	return RETRO_BGM_OK;
}

#define SONG_FINE 0xB1
#define SONG_GOTO 0xB2
#define SONG_PATT 0xB3
#define SONG_REPT 0xB5
#define TRACK_SCAN_MAX 0x10000
#define SONG_TRACKS_MAX 16

static uint32_t read32(RealtimeBgmRead8 read8, void* ctx, uint32_t a) {
	return read8(ctx, a) | read8(ctx, a + 1) << 8 | read8(ctx, a + 2) << 16 | (uint32_t) read8(ctx, a + 3) << 24;
}

static bool trackLoops(RealtimeBgmRead8 read8, void* ctx, uint32_t track) {
	for (uint32_t a = track; a - track < TRACK_SCAN_MAX; ++a) {
		switch (read8(ctx, a)) {
		case SONG_FINE:
			return false;
		case SONG_GOTO: {
			uint32_t to = read32(read8, ctx, a + 1);
			if (to >= track && to < a) {
				return true;
			}
			a += 4;
			break;
		}
		case SONG_PATT:
			a += 4;
			break;
		case SONG_REPT: // count, then pointer
			a += 5;
			break;
		default:
			break;
		}
	}
	return false;
}

bool RealtimeBgmSongLoops(RealtimeBgmRead8 read8, void* ctx, uint32_t songHeader) {
	unsigned tracks = read8(ctx, songHeader);
	if (tracks > SONG_TRACKS_MAX) {
		tracks = SONG_TRACKS_MAX;
	}
	for (unsigned t = 0; t < tracks; ++t) {
		if (trackLoops(read8, ctx, read32(read8, ctx, songHeader + 8 + t * 4))) {
			return true;
		}
	}
	return false;
}
