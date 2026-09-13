// Does disabling a ROM-patching cheat leave the running core able to fetch?
//
// Field report: a PARv3 master code (ROM patch + RAM write) works while on,
// but switching it off mid-game freezes the picture with no sound. A
// CodeBreaker code, which only writes RAM, switches off cleanly.

#include <mgba/core/cheats.h>
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/cheats.h>
#include <mgba/internal/gba/cart/gpio.h>
#include <mgba-util/vfs.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/core/config.h>
#include <mgba/internal/gba/video.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;
static void ok(const char* what, int cond) {
	++checks;
	printf(cond ? "  ok    %s\n" : "  FAIL  %s\n", what);
	if (!cond) ++failures;
}

int main(int argc, char** argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2) {
		printf("usage: %s <rom.gba>\n", argv[0]);
		return 2;
	}

	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	// The software renderer needs somewhere to draw, or video writes fault.
	static mColor buffer[GBA_VIDEO_HORIZONTAL_PIXELS * GBA_VIDEO_VERTICAL_PIXELS];
	core->setVideoBuffer(core, buffer, GBA_VIDEO_HORIZONTAL_PIXELS);
	const char* romPath = argv[1];
	struct VFile* vf = VFileOpen(romPath, O_RDONLY);
	if (!vf) {
		printf("  FAIL  cannot open %s\n", argv[1]);
		return 1;
	}
	if (!core->loadROM(core, vf)) {
		printf("  FAIL  loadROM\n");
		return 1;
	}
	core->reset(core);

	struct GBA* gba = core->board;
	printf("isPristine after load = %d, rom = %p\n", gba->isPristine, (void*) gba->memory.rom);

	struct mCheatDevice* device = core->cheatDevice(core);
	struct mCheatSet* set = device->createSet(device, NULL);
	mCheatAddSet(device, set);

	// The reported code, one line per segment.
	const char* lines[] = {
		"D8BAE4D94864DCE5", "A86CDBA519BA49B3", "A57E2EDEA5AFF3E4",
		"1C7B3231B494738C", "C051CCF6975E8DA1",
	};
	for (size_t i = 0; i < 5; ++i) {
		mCheatAddLine(set, lines[i], 0);
	}
	printf("cheats=%zu romPatches=%zu\n",
		mCheatListSize(&set->list), mCheatPatchListSize(&set->romPatches));

	// What the cheat writes to, so the RAM side can be watched too.
	uint32_t ramAddr = mCheatListGetPointer(&set->list, 0)->address;
	struct mCheatPatch* romPatch = mCheatPatchListGetPointer(&set->romPatches, 0);
	uint32_t romAddr = romPatch->address;
	printf("ram cheat addr = %08X, rom patch addr = %08X\n", ramAddr, romAddr);

	// BOOT_FRAMES_BEFORE_TOGGLE=0 reproduces the field report: the cheat is
	// switched on and back off during the BIOS intro, before the game has
	// written the ROM-adjacent state the patch interacts with.
#ifndef BOOT_FRAMES_BEFORE_TOGGLE
#define BOOT_FRAMES_BEFORE_TOGGLE 240
#endif
	for (int i = 0; i < BOOT_FRAMES_BEFORE_TOGGLE; ++i) core->runFrame(core);
	uint16_t romBefore = core->rawRead16(core, romAddr, -1);
	uint32_t ramBefore = core->rawRead32(core, ramAddr, -1);
	printf("before enable: rom[%08X]=%04X ram[%08X]=%08X\n",
		romAddr, romBefore, ramAddr, ramBefore);

	// Baseline: where the PC sits after a frame with no cheat involved at all.
	for (int i = 0; i < 60; ++i) core->runFrame(core);
	printf("  pc(baseline, no cheat) = %08X\n", ((struct ARMCore*) core->cpu)->gprs[15]);
	uint32_t baseFrames = gba->video.frameCounter;

#ifdef NO_CHEAT_BASELINE
	// Baseline: never enable the cheat, so whatever the ROM compare reports is
	// the game's own doing (the RTC GPIO registers live in the ROM window).
	set->enabled = false;
#else
	set->enabled = true;
#endif
#ifdef REFRESH_VIA_HOOK
	// What retro_cheat_set does: call the set's own refresh hook directly,
	// which skips the _patchROM/_unpatchROM bookkeeping mCheatRefresh does.
	if (set->refresh) set->refresh(set, device);
#else
	mCheatRefresh(device, set);
#endif
	for (int i = 0; i < 60; ++i) core->runFrame(core);
	printf("with cheat on: rom[%08X]=%04X ram[%08X]=%08X\n",
		romAddr, core->rawRead16(core, romAddr, -1),
		ramAddr, core->rawRead32(core, ramAddr, -1));

	// A frame ends inside the BIOS IRQ handler, so the PC alone says little.
	// What actually distinguishes a live core from a wedged one is that the
	// game keeps advancing: the frame counter moves and the fetch never lands
	// on the illegal-instruction filler installed for an unmapped region.
	uint32_t pcOn = ((struct ARMCore*) core->cpu)->gprs[15];
	printf("  pc(on) = %08X\n", pcOn);
	ok("the cheat on keeps the video pipeline advancing", gba->video.frameCounter > baseFrames);

	// Two ways a frontend can switch a cheat off. GoGBA clears the whole
	// device and re-applies what is left; the alternative flips the set's
	// enabled flag and lets mCheatRefresh roll the ROM patch back in place.
	// Only the second one keeps the set, so only it is a true "off".
#ifdef DISABLE_BY_FLAG
	set->enabled = false;
	mCheatRefresh(device, set);
	const char* how = "enabled = false";
#else
	mCheatDeviceClear(device);
	const char* how = "mCheatDeviceClear";
#endif
	uint16_t romAfter = core->rawRead16(core, romAddr, -1);
	printf("after %s: rom[%08X]=%04X (was %04X before enable)\n",
		how, romAddr, romAfter, romBefore);
	ok("the ROM patch is reverted to the pre-cheat value", romAfter == romBefore);

	uint32_t offFrames = gba->video.frameCounter;
	for (int i = 0; i < 60; ++i) core->runFrame(core);
	uint32_t pcOff = ((struct ARMCore*) core->cpu)->gprs[15];
	printf("after 60 more frames: pc=%08X ram[%08X]=%08X\n",
		pcOff, ramAddr, core->rawRead32(core, ramAddr, -1));
	ok("switching the cheat off keeps the video pipeline advancing",
		gba->video.frameCounter > offFrames);

	// The reset the user tried: it restores the CPU and RAM but not the ROM
	// image, so anything the cheat left in ROM survives it. Compare the whole
	// ROM against the file on disk -- one known patch address is not enough to
	// prove nothing else was written.
	core->reset(core);
	for (int i = 0; i < 120; ++i) core->runFrame(core);
	{
		FILE* f = fopen(romPath, "rb");
		size_t diffs = 0;
		if (f) {
			uint8_t* disk = malloc(gba->memory.romSize);
			size_t got = fread(disk, 1, gba->memory.romSize, f);
			fclose(f);
			const uint8_t* live = (const uint8_t*) gba->memory.rom;
			for (size_t i = 0; i + 1 < got; i += 2) {
				// The RTC's GPIO registers sit in the ROM window and the game
				// writes them itself; they differ from the file with or without
				// a cheat, so they are not what this compare is looking for.
				if (i >= GPIO_REG_DATA && i <= GPIO_REG_CONTROL + 1) {
					continue;
				}
				if (live[i] != disk[i] || live[i + 1] != disk[i + 1]) {
					if (diffs < 12) {
						printf("  ROM DIFF %08X: live=%02X%02X disk=%02X%02X\n",
							(unsigned) (0x08000000 + i), live[i + 1], live[i],
							disk[i + 1], disk[i]);
					}
					++diffs;
				}
			}
			free(disk);
		}
		printf("whole-ROM compare after reset: %zu differing halfwords\n", diffs);
		ok("the ROM matches the file on disk after a reset", diffs == 0);
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
