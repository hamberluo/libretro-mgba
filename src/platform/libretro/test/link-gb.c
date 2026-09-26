/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GB link mode. The GB lockstep leaves pacing to the frontend: a slave grants
// itself cycles while idle, which a thread paced to real time used to bound.
// Run from one thread, keeping that grant lets the slave bank time and the
// pair run ahead of the frames asked for -- the game speeds up. Also covers
// per-player saves, double speed and determinism.

#include "../link.h"
#include "link-rom.h"

#include <mgba/internal/gb/gb.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define FRAME_TICKS 140448 // GB_VIDEO_TOTAL_LENGTH << 1: DMG and CGB alike
#define SAVE_SIZE 0x2000

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t rom[LINK_GB_ROM_SIZE];
static uint8_t saves[RETRO_LINK_MAX_PLAYERS][SAVE_SIZE];
static mColor localVideo[256 * 224];

static struct RetroLink* makeLink(void) {
	struct RetroLinkSave s[RETRO_LINK_MAX_PLAYERS];
	unsigned i;
	for (i = 0; i < RETRO_LINK_MAX_PLAYERS; ++i) {
		memset(saves[i], 0xFF, SAVE_SIZE);
		saves[i][0] = 0x11 * (i + 1); // 0x11 for P1, 0x22 for P2
		s[i].data = saves[i];
		s[i].size = SAVE_SIZE;
	}
	return RetroLinkCreate(mPLATFORM_GB, rom, LINK_GB_ROM_SIZE, 2, 0, s, 1700000000000LL, localVideo, 256);
}

static int32_t now(struct mCore* core) {
	return mTimingCurrentTime(&((struct GB*) core->board)->timing);
}

static void _timeout(int sig) {
	UNUSED(sig);
	static const char msg[] = "FAIL: a halted core never returned from its frame\n";
	write(STDOUT_FILENO, msg, sizeof(msg) - 1);
	_exit(1);
}

static void endAndFree(struct RetroLink* link) {
	struct mCore* local = RetroLinkEnd(link);
	mCoreConfigDeinit(&local->config);
	local->deinit(local);
}

static uint32_t runAndChecksum(int frames) {
	struct RetroLink* link = makeLink();
	int f;
	for (f = 0; f < frames; ++f) {
		uint16_t masks[2] = { (uint16_t) (f & 0xFF), (uint16_t) ((f * 7) & 0xFF) };
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
	}
	uint32_t crc = RetroLinkChecksum(link);
	endAndFree(link);
	return crc;
}

int main(void) {
	linkBuildGbRom(rom, false);

	// Pacing: one step is one frame, for both cores, however the slave idles.
	struct RetroLink* link = makeLink();
	CHECK(link, "RetroLinkCreate refused a valid GB ROM");
	struct mCore* a = RetroLinkCore(link, 0);
	struct mCore* b = RetroLinkCore(link, 1);
	int32_t startA = now(a), startB = now(b);
	int32_t worstA = 0, worstB = 0;
	int f;
	for (f = 0; f < 3000; ++f) {
		uint16_t masks[2] = { 0, 0 };
		RetroLinkSetInput(link, masks);
		CHECK(RetroLinkRunFrame(link), "frame %d: every core blocked", f);
		// How far past this step's frame each core ran: a core that finished
		// its frame early must wait, or it runs into the next one and the
		// local screen gets two frames in one step and none in the next.
		int32_t overA = now(a) - startA - (f + 1) * FRAME_TICKS;
		int32_t overB = now(b) - startB - (f + 1) * FRAME_TICKS;
		worstA = overA > worstA ? overA : worstA;
		worstB = overB > worstB ? overB : worstB;
	}
	CHECK(worstA < FRAME_TICKS * 15 / 100 && worstB < FRAME_TICKS * 15 / 100,
	      "a step overran its frame by up to %.2f / %.2f frames", worstA / (double) FRAME_TICKS, worstB / (double) FRAME_TICKS);
	int32_t ranA = now(a) - startA, ranB = now(b) - startB;
	CHECK(ranA < 3004 * FRAME_TICKS && ranB < 3004 * FRAME_TICKS,
	      "3000 steps ran %.1f / %.1f frames", ranA / (double) FRAME_TICKS, ranB / (double) FRAME_TICKS);
	CHECK(abs(ranA - ranB) <= 2 * FRAME_TICKS, "cores drifted %.1f frames apart", abs(ranA - ranB) / (double) FRAME_TICKS);
	CHECK(a->busRead8(a, LINK_GB_COUNT) != 0 && b->busRead8(b, LINK_GB_COUNT) != 0, "no transfers started");

	// Each core booted with its own player's save, and writes back into it.
	CHECK(a->busRead8(a, LINK_GB_SAVE0) == 0x11 && b->busRead8(b, LINK_GB_SAVE0) == 0x22,
	      "saves crossed: P1 booted with %02X, P2 with %02X", a->busRead8(a, LINK_GB_SAVE0), b->busRead8(b, LINK_GB_SAVE0));
	CHECK(saves[0][1] == a->busRead8(a, LINK_GB_COUNT), "P1's save buffer did not receive the cartridge RAM write");
	struct mCore* alone = RetroLinkEnd(link);
	CHECK(!((struct GB*) alone->board)->memory.rotation, "local core still points at the freed link's tilt sensor");
	mCoreConfigDeinit(&alone->config);
	alone->deinit(alone);

	// Determinism.
	CHECK(runAndChecksum(600) == runAndChecksum(600), "same input, different checksums");

	// Double speed: still one frame per step.
	linkBuildGbRom(rom, true);
	link = makeLink();
	a = RetroLinkCore(link, 0);
	startA = now(a);
	for (f = 0; f < 600; ++f) {
		uint16_t masks[2] = { 0, 0 };
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
	}
	ranA = now(a) - startA;
	CHECK(((struct GB*) a->board)->doubleSpeed, "the ROM did not switch to double speed");
	CHECK(ranA >= 600 * FRAME_TICKS && ranA < 603 * FRAME_TICKS, "double speed: 600 steps ran %.1f frames", ranA / (double) FRAME_TICKS);
	endAndFree(link);

	// Both cores halted with nothing to wake them: each step must still end at
	// its frame instead of letting a halted core skip ahead for ever.
	linkBuildGbHaltRom(rom);
	link = makeLink();
	a = RetroLinkCore(link, 0);
	b = RetroLinkCore(link, 1);
	startA = now(a);
	startB = now(b);
	signal(SIGALRM, _timeout);
	alarm(20);
	for (f = 0; f < 60; ++f) {
		uint16_t masks[2] = { 0, 0 };
		RetroLinkSetInput(link, masks);
		RetroLinkRunFrame(link);
	}
	alarm(0);
	ranA = now(a) - startA;
	ranB = now(b) - startB;
	CHECK(ranA < 61 * FRAME_TICKS && ranB < 61 * FRAME_TICKS, "halted: 60 steps ran %.1f / %.1f frames",
	      ranA / (double) FRAME_TICKS, ranB / (double) FRAME_TICKS);
	endAndFree(link);

	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
