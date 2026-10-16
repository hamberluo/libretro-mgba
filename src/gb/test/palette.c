/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A custom DMG palette must reach every monochrome picture: SGB-enhanced carts
// (whose own PAL packets would otherwise recolor them) and states saved under
// a different palette. CGB carts keep their own colors.

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/gb/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba/internal/gb/serialize.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

enum CartKind { CART_DMG, CART_SGB, CART_CGB };

static const uint32_t gray8[4] = { 0xF8F8F8, 0xA8A8A8, 0x505050, 0x000000 };
static const uint32_t green8[4] = { 0x88A048, 0x486830, 0x284020, 0x182808 };
static const uint16_t green5[4] = { 0x2691, 0x19A9, 0x1105, 0x04A3 };

static uint8_t rom[0x8000];

static struct mCore* boot(enum CartKind kind) {
	memset(rom, 0, sizeof(rom));
	static const uint8_t logoStart[4] = { 0xCE, 0xED, 0x66, 0x66 };
	memcpy(&rom[0x104], logoStart, sizeof(logoStart));
	if (kind == CART_SGB) {
		rom[0x146] = 0x03;
		rom[0x14B] = 0x33;
	} else if (kind == CART_CGB) {
		rom[0x143] = 0x80;
	}
	struct mCore* core = GBCoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	return core;
}

// Mirrors what libretro's _updateGbPal does on every variable update.
static void setPal(struct mCore* core, const uint32_t colors[4], bool custom) {
	char key[16];
	int i;
	for (i = 0; i < 12; ++i) {
		snprintf(key, sizeof(key), "gb.pal[%d]", i);
		mCoreConfigSetIntValue(&core->config, key, colors[i & 3]);
	}
	mCoreConfigSetIntValue(&core->config, "sgb.customPalette", custom);
	core->reloadConfigOption(core, "sgb.customPalette", NULL);
	core->reloadConfigOption(core, "gb.pal", NULL);
}

static void sendPal01(struct GB* gb, uint16_t color) {
	uint8_t packet[16] = { (SGB_PAL01 << 3) | 1 };
	int i;
	for (i = 1; i < 15; i += 2) {
		packet[i] = color & 0xFF;
		packet[i + 1] = color >> 8;
	}
	GBVideoWriteSGBPacket(&gb->video, packet);
}

static bool slotsAreGreen(struct GB* gb) {
	int i;
	for (i = 0; i < 16; ++i) {
		if (gb->video.palette[i] != green5[i & 3]) {
			return false;
		}
	}
	return true;
}

static void testSgb(void) {
	struct mCore* core = boot(CART_SGB);
	struct GB* gb = core->board;
	setPal(core, green8, true);
	core->reset(core);
	CHECK(gb->model & GB_MODEL_SGB, "sgb cart did not detect as SGB");
	CHECK(slotsAreGreen(gb), "sgb_reset_fills_all_slots");

	sendPal01(gb, 0x001F);
	CHECK(slotsAreGreen(gb), "sgb_pal_packet_ignored");

	setPal(core, gray8, false);
	CHECK(slotsAreGreen(gb), "sgb_override_off_keeps_slots");

	sendPal01(gb, 0x001F);
	CHECK(gb->video.palette[1] == 0x001F, "sgb_override_off_applies_packet");
	core->deinit(core);
}

static void testDeserializeDmg(void) {
	struct mCore* core = boot(CART_DMG);
	struct GB* gb = core->board;
	setPal(core, gray8, false);
	core->reset(core);
	GBIOWrite(gb, GB_REG_BGP, 0xE4);

	struct GBSerializedState* state = calloc(1, sizeof(*state));
	GBSerialize(gb, state);
	setPal(core, green8, true);
	GBDeserialize(gb, state);
	free(state);
	CHECK(gb->video.palette[0] == green5[0] && gb->video.palette[1] == green5[1] &&
	      gb->video.palette[2] == green5[2] && gb->video.palette[3] == green5[3],
	      "deserialize_refreshes_dmg");
	core->deinit(core);
}

static void testDeserializeSgb(void) {
	struct mCore* core = boot(CART_SGB);
	struct GB* gb = core->board;
	setPal(core, gray8, false);
	core->reset(core);
	sendPal01(gb, 0x001F);

	struct GBSerializedState* state = calloc(1, sizeof(*state));
	GBSerialize(gb, state);
	setPal(core, green8, true);
	GBDeserialize(gb, state);
	free(state);
	CHECK(slotsAreGreen(gb), "deserialize_refreshes_sgb");
	core->deinit(core);
}

static void testCgb(void) {
	struct mCore* core = boot(CART_CGB);
	struct GB* gb = core->board;
	core->reset(core);
	uint16_t before[64];
	memcpy(before, gb->video.palette, sizeof(before));
	setPal(core, green8, true);
	CHECK(memcmp(before, gb->video.palette, sizeof(before)) == 0, "cgb_ignores_custom_palette");
	core->deinit(core);
}

int main(void) {
	testSgb();
	testDeserializeDmg();
	testDeserializeSgb();
	testCgb();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
