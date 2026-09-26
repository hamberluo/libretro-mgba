/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// libretro.c brings its own version strings, and the real inih is linked,
// so only memory mapping is stubbed -- with calloc, so ASan sees the real
// lifetime of every buffer the link glue hands around.

#include <mgba-util/common.h>

void* anonymousMemoryMap(size_t size) {
	return calloc(1, size);
}

void mappedMemoryFree(void* memory, size_t size) {
	UNUSED(size);
	free(memory);
}
