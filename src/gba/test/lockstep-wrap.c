/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Two linked GBAs must keep running once the lockstep clock passes 2^31
// cycles (~128 s). The lockstep code compares int32 timestamps as
// `a - b >= 0` and relies on that wrapping. Without -fwrapv the subtraction
// is signed overflow, the optimizer folds it to `a >= b`, the primary never
// advances the shared clock, _untilNextSync wraps to 0 and _lockstepEvent is
// rescheduled at the same cycle forever -- inside one runLoop call, so the
// hang is caught with an alarm rather than a loop bound.

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/lockstep.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/vfs.h>

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#define PLAYERS 2
// 2^31 cycles is ~7645 frames at 280896 cycles per frame.
#define FRAMES 7800

struct TestUser {
	struct mLockstepUser d;
	int id;
	bool asleep;
};

static void _sleep(struct mLockstepUser* user) {
	((struct TestUser*) user)->asleep = true;
}

static void _wake(struct mLockstepUser* user) {
	((struct TestUser*) user)->asleep = false;
}

static int _requestedId(struct mLockstepUser* user) {
	return ((struct TestUser*) user)->id;
}

static void _quiet(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	UNUSED(logger);
	UNUSED(category);
	UNUSED(level);
	UNUSED(format);
	UNUSED(args);
}

static void _timeout(int sig) {
	UNUSED(sig);
	static const char msg[] = "FAIL: linked cores stopped advancing (lockstep clock wrap)\n";
	write(STDOUT_FILENO, msg, sizeof(msg) - 1);
	_exit(1);
}

int main(void) {
	// `b .` at the ROM entry point: the HLE BIOS jumps straight here, and the
	// idle loop detector skips the spin, so 2^31 cycles pass in seconds.
	static uint32_t rom[0x100] = { 0xEAFFFFFE };

	struct GBASIOLockstepCoordinator coordinator;
	struct GBASIOLockstepDriver drivers[PLAYERS];
	struct TestUser users[PLAYERS] = {0};
	struct mCore* cores[PLAYERS];
	mColor* video[PLAYERS];

	struct mLogger logger = { .log = _quiet };
	mLogSetDefaultLogger(&logger);

	GBASIOLockstepCoordinatorInit(&coordinator);
	int i;
	for (i = 0; i < PLAYERS; ++i) {
		cores[i] = GBACoreCreate();
		cores[i]->init(cores[i]);
		mCoreInitConfig(cores[i], NULL);
		video[i] = calloc(GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS, sizeof(mColor));
		cores[i]->setVideoBuffer(cores[i], video[i], GBA_VIDEO_HORIZONTAL_PIXELS);
		cores[i]->loadROM(cores[i], VFileFromConstMemory(rom, sizeof(rom)));

		users[i].d.sleep = _sleep;
		users[i].d.wake = _wake;
		users[i].d.requestedId = _requestedId;
		users[i].id = i;
		GBASIOLockstepDriverCreate(&drivers[i], &users[i].d);
		GBASIOLockstepCoordinatorAttach(&coordinator, &drivers[i]);
		cores[i]->setPeripheral(cores[i], mPERIPH_GBA_LINK_PORT, &drivers[i].d);
		cores[i]->reset(cores[i]);
	}

	signal(SIGALRM, _timeout);
	alarm(60);

	// Single-threaded stand-in for the per-core threads a frontend would use:
	// a player the coordinator put to sleep is skipped until it is woken.
	int failures = 0;
	while (cores[0]->frameCounter(cores[0]) < FRAMES || cores[1]->frameCounter(cores[1]) < FRAMES) {
		bool ran = false;
		for (i = 0; i < PLAYERS; ++i) {
			if (!users[i].asleep) {
				cores[i]->runLoop(cores[i]);
				ran = true;
			}
		}
		if (!ran) {
			printf("FAIL: every linked core is asleep\n");
			++failures;
			break;
		}
	}
	alarm(0);

	if (coordinator.nAttached != PLAYERS) {
		printf("FAIL: %d players attached, expected %d\n", coordinator.nAttached, PLAYERS);
		++failures;
	}

	for (i = 0; i < PLAYERS; ++i) {
		mCoreConfigDeinit(&cores[i]->config);
		cores[i]->deinit(cores[i]);
		free(video[i]);
	}
	GBASIOLockstepCoordinatorDeinit(&coordinator);

	printf("2 checks, %d failures\n", failures);
	return failures != 0;
}
