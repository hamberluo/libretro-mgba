/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// With the HLE BIOS, SWI 0xD4 lands where the real BIOS's out-of-range
// jump table sends it: System mode at 0x02002040.

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>

#include <stdio.h>

int main(void) {
	static uint32_t rom[0x80];
	rom[0] = 0xEFD40000; // swi 0xD40000

	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, VFileFromConstMemory(rom, sizeof(rom)));
	core->reset(core);
	GBASkipBIOS(core->board);

	struct ARMCore* cpu = core->cpu;
	core->step(core);

	int failures = 0;
	uint32_t pc = cpu->gprs[ARM_PC] - WORD_SIZE_ARM;
	if (pc != 0x02002040) {
		printf("FAIL: pc=%08X\n", pc);
		++failures;
	}
	if (cpu->cpsr.priv != MODE_SYSTEM) {
		printf("FAIL: mode=%02X\n", cpu->cpsr.priv);
		++failures;
	}
	if (cpu->gprs[ARM_LR] != 0xA4) {
		printf("FAIL: lr=%08X\n", cpu->gprs[ARM_LR]);
		++failures;
	}

	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	printf("swi 0xD4: %d failures\n", failures);
	return failures != 0;
}
