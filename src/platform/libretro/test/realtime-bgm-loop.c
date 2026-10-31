/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "../realtime-bgm.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define BASE 0x08100000u
#define FINE 0xB1
#define GOTO 0xB2
#define W96 0xB0

static uint8_t mem[0x200];

static uint8_t read8(void* ctx, uint32_t a) {
	(void) ctx;
	return a >= BASE && a - BASE < sizeof(mem) ? mem[a - BASE] : 0;
}

static void put32(size_t at, uint32_t v) {
	for (int i = 0; i < 4; ++i) mem[at + i] = (uint8_t) (v >> (8 * i));
}

// Header at 0: track count, then track pointers from +8.
static void song(unsigned tracks, const uint32_t* trackOffsets) {
	memset(mem, 0, sizeof(mem));
	mem[0] = (uint8_t) tracks;
	for (unsigned t = 0; t < tracks; ++t) put32(8 + t * 4, BASE + trackOffsets[t]);
}

// A PATT before the loop points at an address whose low byte is 0xB1; read
// byte by byte, that argument would look like FINE and end the parse early.
static void loop_pointer_byte_is_not_fine(void) {
	uint32_t trk[] = { 0x40 };
	song(1, trk);
	size_t t = trk[0];
	mem[t] = 0xB3; put32(t + 1, BASE + 0xB1); // PATT
	mem[t + 5] = 0xD0; mem[t + 6] = 0x3C;
	mem[t + 7] = GOTO; put32(t + 8, BASE + t);
	mem[t + 12] = FINE;
	CHECK(RealtimeBgmSongLoops(read8, NULL, BASE));
}

// Kirby pads `GOTO ptr W96 FINE`.
static void loop_goto_wait_fine(void) {
	uint32_t trk[] = { 0x40 };
	song(1, trk);
	size_t t = trk[0];
	mem[t] = 0xD0; mem[t + 1] = 0x3C;
	mem[t + 2] = GOTO; put32(t + 3, BASE + t);
	mem[t + 7] = W96;
	mem[t + 8] = FINE;
	CHECK(RealtimeBgmSongLoops(read8, NULL, BASE));
}

// Heart and Soul loops on a later track only.
static void loop_on_track_1_only(void) {
	uint32_t trk[] = { 0x40, 0x60 };
	song(2, trk);
	mem[0x40] = 0xD0; mem[0x41] = 0x3C; mem[0x42] = FINE;
	mem[0x60] = 0xD0; mem[0x61] = 0x3C; mem[0x62] = GOTO; put32(0x63, BASE + 0x60); mem[0x67] = FINE;
	CHECK(RealtimeBgmSongLoops(read8, NULL, BASE));
}

static void loop_fine_only(void) {
	uint32_t trk[] = { 0x40 };
	song(1, trk);
	mem[0x40] = 0xD0; mem[0x41] = 0x3C; mem[0x42] = W96; mem[0x43] = FINE;
	CHECK(!RealtimeBgmSongLoops(read8, NULL, BASE));
}

int main(void) {
	loop_pointer_byte_is_not_fine();
	loop_goto_wait_fine();
	loop_on_track_1_only();
	loop_fine_only();
	if (failures) {
		fprintf(stderr, "realtime-bgm-loop: %d failure(s)\n", failures);
		return 1;
	}
	printf("realtime-bgm-loop: ok\n");
	return 0;
}
