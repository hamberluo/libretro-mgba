/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "link.h"

#include <mgba/core/config.h>
#include <mgba/core/interface.h>
#include <mgba/core/lockstep.h>
#include <mgba/core/timing.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/crc32.h>
#include <mgba-util/vfs.h>
#ifdef M_CORE_GBA
#include <mgba/gba/interface.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/sio.h>
#include <mgba/internal/gb/sio/lockstep.h>
#include <mgba/internal/sm83/sm83.h>
#endif

#include <limits.h>

#include "libretro.h"

struct GoGBALinkPlayer {
	struct mLockstepUser user; // first: the GBA driver hands it back to our callbacks
	struct GoGBALink* link;
	unsigned index;
	struct mCore* core;
	mColor* scratchVideo;      // remote cores need a buffer even though they never draw
	int32_t frameTicks;        // one frame of emulated time, in mTiming ticks
	int32_t deadline;          // where the current frame ends, in mTiming ticks
	bool asleep;               // parked by the coordinator until another core catches up
	int turboClock;
	bool turboDown;
#ifdef M_CORE_GBA
	struct GBASIOLockstepDriver gbaDriver;
#endif
#ifdef M_CORE_GB
	struct GBSIOLockstepNode gbNode;
#endif
};

struct GoGBALink {
	enum mPlatform platform;
	unsigned players;
	unsigned localPlayer;
	struct GoGBALinkPlayer player[GOGBA_LINK_MAX_PLAYERS];
	struct mRotationSource rotation;
#ifdef M_CORE_GBA
	struct GBALuminanceSource lux;
	struct GBASIOLockstepCoordinator gbaCoordinator;
#endif
#ifdef M_CORE_GB
	struct GBSIOLockstep gbLockstep;
	int32_t gbPosted[GOGBA_LINK_MAX_PLAYERS]; // cycles each slave may run, by lockstep id
	unsigned gbWaitMask;
#endif
};

static const int _keymap[] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_LEFT,
	RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_R,
	RETRO_DEVICE_ID_JOYPAD_L,
};

// Sensors are input too, but only sampled on the device holding them; every
// core in a link reads the same constants instead.
static int32_t _neutralTilt(struct mRotationSource* source) {
	UNUSED(source);
	return 0;
}

#ifdef M_CORE_GBA
static uint8_t _neutralLux(struct GBALuminanceSource* source) {
	UNUSED(source);
	return 0;
}
#endif

static struct mTiming* _timing(struct GoGBALinkPlayer* player) {
	switch (player->link->platform) {
#ifdef M_CORE_GBA
	case mPLATFORM_GBA:
		return &((struct GBA*) player->core->board)->timing;
#endif
#ifdef M_CORE_GB
	case mPLATFORM_GB:
		return &((struct GB*) player->core->board)->timing;
#endif
	default:
		return NULL;
	}
}

static void _playerSleep(struct mLockstepUser* user) {
	((struct GoGBALinkPlayer*) user)->asleep = true;
}

static void _playerWake(struct mLockstepUser* user) {
	((struct GoGBALinkPlayer*) user)->asleep = false;
}

static int _playerRequestedId(struct mLockstepUser* user) {
	return ((struct GoGBALinkPlayer*) user)->index;
}

#ifdef M_CORE_GB
static struct GoGBALinkPlayer* _gbPlayerById(struct GoGBALink* link, int id) {
	// The node that starts a transfer takes id 0, so ids and players can swap.
	unsigned i;
	for (i = 0; i < link->players; ++i) {
		if (link->player[i].gbNode.id == id) {
			return &link->player[i];
		}
	}
	return NULL;
}

static void _gbPark(struct GoGBALinkPlayer* player) {
	struct SM83Core* cpu = player->core->cpu;
	player->asleep = true;
	cpu->nextEvent = cpu->cycles; // leave runLoop so the scheduler can switch cores
}

static bool _gbSignal(struct mLockstep* lockstep, unsigned mask) {
	struct GoGBALink* link = lockstep->context;
	int id;
	for (id = 0; id < (int) link->players; ++id) {
		if (!(mask & (1u << id))) {
			continue;
		}
		if (link->gbWaitMask & (1u << id)) {
			link->gbWaitMask &= ~(1u << id);
			if (!link->gbWaitMask) {
				_gbPlayerById(link, 0)->asleep = false;
			}
		}
		// The master signalling lets slaves run; a slave signalling that it
		// caught up does not wake itself.
		if (id != 0 && mask != (1u << id)) {
			_gbPlayerById(link, id)->asleep = false;
		}
	}
	return true;
}

static bool _gbWait(struct mLockstep* lockstep, unsigned mask) {
	struct GoGBALink* link = lockstep->context;
	int id;
	link->gbWaitMask |= mask;
	for (id = 1; id < (int) link->players; ++id) {
		if (mask & (1u << id)) {
			_gbPlayerById(link, id)->asleep = false;
		}
	}
	if (link->gbWaitMask) {
		_gbPark(_gbPlayerById(link, 0));
	}
	return true;
}

static void _gbAddCycles(struct mLockstep* lockstep, int id, int32_t cycles) {
	struct GoGBALink* link = lockstep->context;
	if (id != 0) {
		// A slave granting itself cycles while idle is dropped. Qt's frontend
		// ran each GB on a thread paced to real time, which bounded that grant;
		// from one thread it lets the slave bank time and both run ahead.
		return;
	}
	int i;
	for (i = 1; i < (int) link->players; ++i) {
		link->gbPosted[i] += cycles;
		if (link->gbPosted[i] > 0) {
			_gbPlayerById(link, i)->asleep = false;
		}
	}
}

static int32_t _gbUseCycles(struct mLockstep* lockstep, int id, int32_t cycles) {
	struct GoGBALink* link = lockstep->context;
	link->gbPosted[id] -= cycles;
	if (link->gbPosted[id] <= 0) {
		_gbPark(_gbPlayerById(link, id));
	}
	return link->gbPosted[id];
}

static int32_t _gbUnusedCycles(struct mLockstep* lockstep, int id) {
	return ((struct GoGBALink*) lockstep->context)->gbPosted[id];
}

static void _gbUnload(struct mLockstep* lockstep, int id) {
	UNUSED(lockstep);
	UNUSED(id);
}
#endif

// Anything that changes emulated results is pinned here instead of read from
// the frontend, so both ends agree whatever their settings say.
static void _configure(struct mCore* core) {
	struct mCoreOptions opts = {
		.useBios = false,
		.skipBios = true,
		.volume = 0x100,
	};
	mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
	mCoreConfigSetDefaultIntValue(&core->config, "allowOpposingDirections", 0);
	mCoreConfigSetDefaultIntValue(&core->config, "sgb.borders", 0);
	mCoreConfigLoadDefaults(&core->config, &opts);
	mCoreLoadConfig(core);
}

static void _neverDraw(struct GoGBALinkPlayer* player) {
	switch (player->link->platform) {
#ifdef M_CORE_GBA
	case mPLATFORM_GBA:
		((struct GBA*) player->core->board)->video.frameskip = INT_MAX;
		((struct GBA*) player->core->board)->video.frameskipCounter = INT_MAX;
		break;
#endif
#ifdef M_CORE_GB
	case mPLATFORM_GB:
		((struct GB*) player->core->board)->video.frameskip = INT_MAX;
		((struct GB*) player->core->board)->video.frameskipCounter = INT_MAX;
		break;
#endif
	default:
		break;
	}
}

static void _destroyPlayer(struct GoGBALinkPlayer* player) {
	if (player->core) {
		mCoreConfigDeinit(&player->core->config);
		player->core->deinit(player->core);
		player->core = NULL;
	}
	free(player->scratchVideo);
	player->scratchVideo = NULL;
}

static bool _plugIn(struct GoGBALinkPlayer* player) {
	struct GoGBALink* link = player->link;
	struct mCore* core = player->core;
	switch (link->platform) {
#ifdef M_CORE_GBA
	case mPLATFORM_GBA:
		core->setPeripheral(core, mPERIPH_GBA_LUMINANCE, &link->lux);
		player->user.sleep = _playerSleep;
		player->user.wake = _playerWake;
		player->user.requestedId = _playerRequestedId;
		GBASIOLockstepDriverCreate(&player->gbaDriver, &player->user);
		GBASIOLockstepCoordinatorAttach(&link->gbaCoordinator, &player->gbaDriver);
		core->setPeripheral(core, mPERIPH_GBA_LINK_PORT, &player->gbaDriver.d);
		core->reset(core);
		return true;
#endif
#ifdef M_CORE_GB
	case mPLATFORM_GB:
		core->reset(core);
		GBSIOLockstepNodeCreate(&player->gbNode);
		GBSIOLockstepAttachNode(&link->gbLockstep, &player->gbNode);
		GBSIOSetDriver(&((struct GB*) core->board)->sio, &player->gbNode.d);
		return true;
#endif
	default:
		return false;
	}
}

struct GoGBALink* GoGBALinkCreate(enum mPlatform platform, const void* rom, size_t romSize,
                                  unsigned players, unsigned localPlayer,
                                  const struct GoGBALinkSave* saves, int64_t rtcEpochMs) {
	if (players < 2 || players > GOGBA_LINK_MAX_PLAYERS || localPlayer >= players || !rom || !saves) {
		return NULL;
	}
	struct GoGBALink* link = calloc(1, sizeof(*link));
	link->platform = platform;
	link->players = players;
	link->localPlayer = localPlayer;
	link->rotation.readTiltX = _neutralTilt;
	link->rotation.readTiltY = _neutralTilt;
	link->rotation.readGyroZ = _neutralTilt;
#ifdef M_CORE_GBA
	link->lux.readLuminance = _neutralLux;
	GBASIOLockstepCoordinatorInit(&link->gbaCoordinator);
#endif
#ifdef M_CORE_GB
	GBSIOLockstepInit(&link->gbLockstep);
	link->gbLockstep.d.context = link;
	link->gbLockstep.d.signal = _gbSignal;
	link->gbLockstep.d.wait = _gbWait;
	link->gbLockstep.d.addCycles = _gbAddCycles;
	link->gbLockstep.d.useCycles = _gbUseCycles;
	link->gbLockstep.d.unusedCycles = _gbUnusedCycles;
	link->gbLockstep.d.unload = _gbUnload;
#endif

	unsigned i;
	for (i = 0; i < players; ++i) {
		struct GoGBALinkPlayer* player = &link->player[i];
		player->link = link;
		player->index = i;
		player->turboDown = true; // libretro.c's cycleturbo starts pressed
		player->core = mCoreCreate(platform);
		if (!player->core) {
			goto fail;
		}
		mCoreInitConfig(player->core, NULL);
		player->core->init(player->core);
		_configure(player->core);
		if (!player->core->loadROM(player->core, VFileFromConstMemory(rom, romSize))) {
			goto fail;
		}
		player->core->rtc.override = RTC_FAKE_EPOCH;
		player->core->rtc.value = rtcEpochMs;
		player->core->setPeripheral(player->core, mPERIPH_ROTATION, &link->rotation);
		if (i != localPlayer) {
			player->scratchVideo = calloc(256 * 224, sizeof(mColor));
			player->core->setVideoBuffer(player->core, player->scratchVideo, 256);
		}
		if (!_plugIn(player)) {
			goto fail;
		}
		struct VFile* save = VFileFromMemory(saves[i].data, saves[i].size);
		if (!player->core->loadSave(player->core, save)) {
			save->close(save);
		}
		if (i != localPlayer) {
			_neverDraw(player);
		}
		player->frameTicks = (int32_t) ((int64_t) player->core->frameCycles(player->core)
		                                * player->core->timingFrequency(player->core)
		                                / player->core->frequency(player->core));
		player->deadline = mTimingCurrentTime(_timing(player));
	}
	return link;

fail:
	for (i = 0; i < players; ++i) {
		_destroyPlayer(&link->player[i]);
	}
#ifdef M_CORE_GBA
	GBASIOLockstepCoordinatorDeinit(&link->gbaCoordinator);
#endif
	free(link);
	return NULL;
}

struct mCore* GoGBALinkCore(struct GoGBALink* link, unsigned player) {
	return player < link->players ? link->player[player].core : NULL;
}

struct mCore* GoGBALinkLocalCore(struct GoGBALink* link) {
	return link->player[link->localPlayer].core;
}

// libretro.c's keymap, plus its turbo cadence kept per player so each core
// sees its own player's turbo exactly as it would in single player.
static uint16_t _keysFromMask(struct GoGBALinkPlayer* player, uint16_t mask) {
	uint16_t keys = 0;
	size_t i;
	for (i = 0; i < sizeof(_keymap) / sizeof(*_keymap); ++i) {
		keys |= ((mask >> _keymap[i]) & 1) << i;
	}
	if (++player->turboClock >= 2) {
		player->turboClock = 0;
		player->turboDown = !player->turboDown;
	}
	if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_X)) {
		keys |= player->turboDown << 0;
	}
	if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_Y)) {
		keys |= player->turboDown << 1;
	}
	if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_L2)) {
		keys |= player->turboDown << 9;
	}
	if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_R2)) {
		keys |= player->turboDown << 8;
	}
	return keys;
}

void GoGBALinkSetInput(struct GoGBALink* link, const uint16_t* joypadMasks) {
	unsigned i;
	for (i = 0; i < link->players; ++i) {
		struct GoGBALinkPlayer* player = &link->player[i];
		player->core->setKeys(player->core, _keysFromMask(player, joypadMasks[i]));
	}
}

bool GoGBALinkRunFrame(struct GoGBALink* link) {
	int32_t target[GOGBA_LINK_MAX_PLAYERS];
	unsigned i;
	for (i = 0; i < link->players; ++i) {
		// Absolute deadlines: counting from wherever the last frame overshot
		// would add that overshoot every frame, and the game would run fast.
		link->player[i].deadline += link->player[i].frameTicks;
		target[i] = link->player[i].deadline;
	}
	while (true) {
		bool done = true;
		for (i = 0; i < link->players; ++i) {
			if (mTimingCurrentTime(_timing(&link->player[i])) - target[i] < 0) {
				done = false;
			}
		}
		if (done) {
			break;
		}
		// Measured in emulated time, not frameCounter: a GB turning its LCD off
		// and on drops part of a frame, so frame counts drift between cores.
		// A core that reached its target waits, unless every core still short
		// of it is parked by the cable and only an early one can free them.
		bool ran = false;
		int pass;
		for (pass = 0; pass < 2 && !ran; ++pass) {
			for (i = 0; i < link->players; ++i) {
				struct GoGBALinkPlayer* player = &link->player[i];
				if (player->asleep) {
					continue;
				}
				if (pass == 0 && mTimingCurrentTime(_timing(player)) - target[i] >= 0) {
					continue;
				}
				player->core->runLoop(player->core);
				ran = true;
			}
		}
		if (!ran) {
			return false;
		}
	}
	for (i = 0; i < link->players; ++i) {
		if (i != link->localPlayer) {
			mAudioBufferClear(link->player[i].core->getAudioBuffer(link->player[i].core));
		}
	}
	return true;
}

uint32_t GoGBALinkChecksum(struct GoGBALink* link) {
	uint32_t crc = 0;
	unsigned i;
	for (i = 0; i < link->players; ++i) {
		struct mCore* core = link->player[i].core;
		switch (link->platform) {
#ifdef M_CORE_GBA
		case mPLATFORM_GBA: {
			struct GBA* gba = core->board;
			crc = crc32(crc, gba->memory.wram, GBA_SIZE_EWRAM);
			crc = crc32(crc, gba->memory.iwram, GBA_SIZE_IWRAM);
			crc = crc32(crc, gba->memory.io, sizeof(gba->memory.io));
			crc = crc32(crc, gba->cpu->gprs, sizeof(gba->cpu->gprs));
			break;
		}
#endif
	#ifdef M_CORE_GB
		case mPLATFORM_GB: {
			struct GB* gb = core->board;
			crc = crc32(crc, gb->memory.wram, GB_SIZE_WORKING_RAM);
			crc = crc32(crc, gb->memory.hram, GB_SIZE_HRAM);
			crc = crc32(crc, gb->memory.io, GB_SIZE_IO);
			break;
		}
#endif
	default:
			break;
		}
	}
	return crc;
}
static void _unplug(struct GoGBALinkPlayer* player) {
	switch (player->link->platform) {
#ifdef M_CORE_GBA
	case mPLATFORM_GBA:
		// Setting no driver deinits the lockstep one, which removes the player
		// from the coordinator and wakes whoever was waiting on it.
		player->core->setPeripheral(player->core, mPERIPH_GBA_LINK_PORT, NULL);
		break;
#endif
#ifdef M_CORE_GB
	case mPLATFORM_GB:
		GBSIOSetDriver(&((struct GB*) player->core->board)->sio, NULL);
		GBSIOLockstepDetachNode(&player->link->gbLockstep, &player->gbNode);
		break;
#endif
	default:
		break;
	}
	player->asleep = false;
}

struct mCore* GoGBALinkEnd(struct GoGBALink* link) {
	unsigned i;
	for (i = 0; i < link->players; ++i) {
		_unplug(&link->player[i]);
	}
	struct mCore* local = link->player[link->localPlayer].core;
	link->player[link->localPlayer].core = NULL;
	for (i = 0; i < link->players; ++i) {
		_destroyPlayer(&link->player[i]);
	}
#ifdef M_CORE_GBA
	GBASIOLockstepCoordinatorDeinit(&link->gbaCoordinator);
#endif
	free(link);
	return local;
}
