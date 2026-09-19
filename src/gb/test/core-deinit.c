/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Deinit destroys the cheat device before tearing the CPU down, so a patched
// ROM is reverted while the memory it writes through is still mapped. Leaving
// the device in components[] made SM83Deinit call deinit on it afterwards,
// through freed memory -- retro_unload_game crashed there. Needs ASan: the
// call lands on reused heap and returns normally without it.

#include <mgba/core/core.h>
#include <mgba/gb/core.h>

#include <stdio.h>

int main(void) {
	struct mCore* core = GBCoreCreate();
	core->init(core);
	core->cheatDevice(core);
	core->deinit(core);

	printf("1 checks, 0 failures\n");
	return 0;
}
