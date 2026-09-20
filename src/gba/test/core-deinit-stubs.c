/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Both cores are linked for real here -- the GBA core under test, and the GB
// core the shared mCore loader names -- so only the leaves outside them are
// stubbed: the config layer's ini parser and version strings.

#include <mgba-util/common.h>

int ini_parse_stream(void) { return -1; }

const char* const projectName = "mgba-test";
const char* const projectVersion = "0";

// ASan has to see the cheat device's real lifetime, so no mmap here.
void* anonymousMemoryMap(size_t size) { return calloc(1, size); }

void mappedMemoryFree(void* memory, size_t size) {
	UNUSED(size);
	free(memory);
}
