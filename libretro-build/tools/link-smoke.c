/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Dev tool: 2-4 linked cores on a real ROM (never committed), driven by a
// script, reporting drift, SIO modes and screenshots of the viewed player.
// Script lines (players are 0-based):
//   run N | key P A+B+START... (or -) | tap P KEYS [frames] | view P | shot NAME | info
// usage: link-smoke rom script [players]

#include "../../src/platform/libretro/link.h"

#include <mgba/core/log.h>
#ifdef M_CORE_GBA
#include <mgba/internal/gba/gba.h>
#endif
#ifdef M_CORE_GB
#include <mgba/internal/gb/gb.h>
#endif
#include <mgba-util/vfs.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/platform/libretro/libretro.h"

static void _quiet(struct mLogger* l, int c, enum mLogLevel v, const char* f, va_list a) {
	UNUSED(l); UNUSED(c); UNUSED(v); UNUSED(f); UNUSED(a);
}

static uint16_t masks[RETRO_LINK_MAX_PLAYERS];
static unsigned players = 2;
static unsigned modesSeen; // GBA: bit per GBASIOMode observed on player 0
static mColor video[256 * 224]; // the viewed player's screen

static uint16_t parseKeys(const char* s) {
	static const struct { const char* name; int id; } keys[] = {
		{ "A", RETRO_DEVICE_ID_JOYPAD_A }, { "B", RETRO_DEVICE_ID_JOYPAD_B },
		{ "SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT }, { "START", RETRO_DEVICE_ID_JOYPAD_START },
		{ "RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT }, { "LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT },
		{ "UP", RETRO_DEVICE_ID_JOYPAD_UP }, { "DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN },
		{ "R", RETRO_DEVICE_ID_JOYPAD_R }, { "L", RETRO_DEVICE_ID_JOYPAD_L },
	};
	uint16_t mask = 0;
	char buf[128];
	strncpy(buf, s, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	char* tok;
	for (tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
		size_t i;
		for (i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
			if (!strcasecmp(tok, keys[i].name)) {
				mask |= 1 << keys[i].id;
			}
		}
	}
	return mask;
}

static void step(struct RetroLink* link, int frames) {
	int f;
	for (f = 0; f < frames; ++f) {
		RetroLinkSetInput(link, masks);
		if (!RetroLinkRunFrame(link)) {
			printf("DEADLOCK\n");
			exit(1);
		}
#ifdef M_CORE_GBA
		struct mCore* p1 = RetroLinkCore(link, 0);
		if (p1->platform(p1) == mPLATFORM_GBA) {
			modesSeen |= 1u << ((struct GBA*) p1->board)->sio.mode;
		}
#endif
	}
}

static void shot(struct RetroLink* link, const char* name) {
	char path[512];
	unsigned w, h;
	struct mCore* shown = RetroLinkViewCore(link);
	shown->currentVideoSize(shown, &w, &h);
	snprintf(path, sizeof(path), "%s.ppm", name);
	FILE* f = fopen(path, "wb");
	fprintf(f, "P6\n%u %u\n255\n", w, h);
	unsigned x, y;
	for (y = 0; y < h; ++y) {
		for (x = 0; x < w; ++x) {
			uint16_t c = video[y * 256 + x];
			unsigned char px[3] = { (c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3 };
			fwrite(px, 1, 3, f);
		}
	}
	fclose(f);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s rom script [players]\n", argv[0]);
		return 2;
	}
	players = argc > 3 ? (unsigned) atoi(argv[3]) : 2;
	if (players < 2 || players > RETRO_LINK_MAX_PLAYERS) {
		fprintf(stderr, "players must be 2-%d\n", RETRO_LINK_MAX_PLAYERS);
		return 2;
	}
	struct mLogger logger = { .log = _quiet };
	mLogSetDefaultLogger(&logger);
	struct VFile* vf = VFileOpen(argv[1], O_RDONLY);
	if (!vf) {
		fprintf(stderr, "cannot open %s\n", argv[1]);
		return 1;
	}
	size_t size = vf->size(vf);
	void* rom = malloc(size);
	vf->read(vf, rom, size);
	enum mPlatform platform = mCoreIsCompatible(vf);
	vf->close(vf);
	static uint8_t saveBuf[RETRO_LINK_MAX_PLAYERS][0x20000];
	memset(saveBuf, 0xFF, sizeof(saveBuf));
	struct RetroLinkCart carts[RETRO_LINK_MAX_PLAYERS];
	unsigned p;
	for (p = 0; p < players; ++p) {
		carts[p] = (struct RetroLinkCart) { rom, size, saveBuf[p], sizeof(saveBuf[p]) };
	}
	struct RetroLink* link = RetroLinkCreate(platform, carts, players, 0, 1700000000000LL, video, 256);
	if (!link) {
		fprintf(stderr, "link refused the ROM\n");
		return 1;
	}
	FILE* script = fopen(argv[2], "r");
	char line[256];
	while (script && fgets(line, sizeof(line), script)) {
		char op[16] = {0}, a1[128] = {0}, a2[128] = {0}, a3[16] = {0};
		sscanf(line, "%15s %127s %127s %15s", op, a1, a2, a3);
		if (!strcmp(op, "run")) {
			step(link, atoi(a1));
		} else if (!strcmp(op, "key")) {
			masks[atoi(a1)] = parseKeys(a2);
		} else if (!strcmp(op, "tap")) {
			masks[atoi(a1)] = parseKeys(a2);
			step(link, a3[0] ? atoi(a3) : 6);
			masks[atoi(a1)] = 0;
			step(link, 6);
		} else if (!strcmp(op, "view")) {
			RetroLinkSetView(link, atoi(a1));
		} else if (!strcmp(op, "shot")) {
			shot(link, a1);
		} else if (!strcmp(op, "info")) {
			struct mCore* a = RetroLinkCore(link, 0);
			struct mCore* b = RetroLinkCore(link, 1);
			printf("frames");
			for (p = 0; p < players; ++p) {
				printf(" %u", RetroLinkCore(link, p)->frameCounter(RetroLinkCore(link, p)));
			}
			printf(", checksum %08X, SIO modes seen 0x%X", RetroLinkChecksum(link), modesSeen);
#ifdef M_CORE_GB
			if (platform == mPLATFORM_GB) {
				printf(", double speed %d / %d", ((struct GB*) a->board)->doubleSpeed, ((struct GB*) b->board)->doubleSpeed);
			}
#endif
			printf("\n");
		}
	}
	struct mCore* local = RetroLinkEnd(link);
	mCoreConfigDeinit(&local->config);
	local->deinit(local);
	free(rom);
	return 0;
}
