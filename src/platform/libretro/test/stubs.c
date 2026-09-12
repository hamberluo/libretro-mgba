/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The GBA cheat parsers reference breakpoints, ROM patching and hashing that
// only matter once a cheat runs. Parsing never reaches them.

#include <mgba-util/common.h>

void GBASetBreakpoint(void) {}
void GBAClearBreakpoint(void) {}
bool mCoreGetMemoryBlockInfo(void) { return false; }
const uint16_t* gbkUnicodeTable = NULL;
uint32_t hash32(const void* key, size_t len, uint32_t seed) {
	UNUSED(key);
	UNUSED(len);
	return seed;
}
