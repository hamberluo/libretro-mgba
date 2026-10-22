/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A MULTI primary that rewrites SIOCNT with Start clear while its transfer is
// in flight drops that transfer. Four Swords does this when its lobby opens
// ~360 cycles before the transfer it just started would finish; if the old
// Busy bit survives the write, the transfer completes into the freshly
// enabled IRQ and the lobby never transfers again, stuck on "Linking with
// other systems" until the player backs out and re-enters.

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/lockstep.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/vfs.h>

#include <stdio.h>

#define PLAYERS 2
#define SIOCNT_MULTI_115200 0x2003
#define SIOCNT_BUSY 0x0080
#define SIOCNT_IRQ 0x4000
#define IRQ_SIO (1 << GBA_IRQ_SIO)

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

static struct mCore* cores[PLAYERS];
static struct TestUser users[PLAYERS];

static bool _runOnce(void) {
	bool ran = false;
	int i;
	for (i = 0; i < PLAYERS; ++i) {
		if (!users[i].asleep) {
			cores[i]->runLoop(cores[i]);
			ran = true;
		}
	}
	return ran;
}

static bool _runFrames(unsigned frames) {
	unsigned target = cores[0]->frameCounter(cores[0]) + frames;
	while (cores[0]->frameCounter(cores[0]) < target || cores[1]->frameCounter(cores[1]) < target) {
		if (!_runOnce()) {
			return false;
		}
	}
	return true;
}

static struct GBA* _gba(int player) {
	return cores[player]->board;
}

int main(void) {
	// `b .` with IME off: the SIO IRQ only latches in IF, which is what we read.
	static uint32_t rom[0x100] = { 0xEAFFFFFE };

	struct GBASIOLockstepCoordinator coordinator;
	struct GBASIOLockstepDriver drivers[PLAYERS];
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

	int failures = 0;
	bool linked = _runFrames(2);
	for (i = 0; i < PLAYERS; ++i) {
		GBAIOWrite(_gba(i), GBA_REG_RCNT, 0);
		GBAIOWrite(_gba(i), GBA_REG_SIOCNT, SIOCNT_MULTI_115200);
	}
	linked = linked && _runFrames(2);

	// Start a transfer and stop as soon as the secondary has taken it: the
	// primary is woken but has not run, so its transfer is still in flight.
	_gba(0)->memory.io[GBA_REG(IF)] = 0;
	GBAIOWrite(_gba(0), GBA_REG_SIOCNT, SIOCNT_MULTI_115200 | SIOCNT_BUSY);
	while (linked && users[0].asleep) {
		linked = _runOnce();
	}
	if (!linked) {
		printf("FAIL: linked cores stalled before the transfer started\n");
		++failures;
	} else if (!(_gba(0)->sio.siocnt & SIOCNT_BUSY)) {
		printf("FAIL: transfer was not in flight when SIOCNT was rewritten\n");
		++failures;
	}

	// What Four Swords writes as its lobby opens: Start clear, IRQ enabled.
	GBAIOWrite(_gba(0), GBA_REG_SIOCNT, SIOCNT_MULTI_115200 | SIOCNT_IRQ);
	linked = linked && _runFrames(2);
	if (_gba(0)->sio.siocnt & SIOCNT_BUSY) {
		printf("FAIL: primary still busy after clearing Start (SIOCNT %04X)\n", _gba(0)->sio.siocnt);
		++failures;
	}
	if (_gba(0)->memory.io[GBA_REG(IF)] & IRQ_SIO) {
		printf("FAIL: the dropped transfer still raised the SIO IRQ\n");
		++failures;
	}

	// The cable must still work after the drop.
	GBAIOWrite(_gba(0), GBA_REG_SIOCNT, SIOCNT_MULTI_115200 | SIOCNT_IRQ | SIOCNT_BUSY);
	linked = linked && _runFrames(2);
	if (!linked) {
		printf("FAIL: linked cores stalled after the dropped transfer\n");
		++failures;
	} else if ((_gba(0)->sio.siocnt & SIOCNT_BUSY) || !(_gba(0)->memory.io[GBA_REG(IF)] & IRQ_SIO)) {
		printf("FAIL: next transfer did not complete (SIOCNT %04X IF %04X)\n",
		       _gba(0)->sio.siocnt, _gba(0)->memory.io[GBA_REG(IF)]);
		++failures;
	}

	for (i = 0; i < PLAYERS; ++i) {
		mCoreConfigDeinit(&cores[i]->config);
		cores[i]->deinit(cores[i]);
		free(video[i]);
	}
	GBASIOLockstepCoordinatorDeinit(&coordinator);

	printf("4 checks, %d failures\n", failures);
	return failures != 0;
}
