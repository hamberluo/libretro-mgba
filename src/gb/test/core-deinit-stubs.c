/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Linking a whole GB core reaches the shared mCore loader, which names the
// GBA core, and the config layer, which names the ini parser and the version
// strings. None of them run on the path under test.

#include <mgba-util/common.h>
#include <mgba/core/core.h>

struct mCore* GBACoreCreate(void) { return NULL; }

bool GBAIsROM(struct VFile* vf) {
	UNUSED(vf);
	return false;
}

void GBAAudioSample(void) {}

int ini_parse_stream(void) { return -1; }

const char* const projectName = "mgba-test";
const char* const projectVersion = "0";

// ASan has to see the cheat device's real lifetime, so no mmap here.
void* anonymousMemoryMap(size_t size) { return calloc(1, size); }

void mappedMemoryFree(void* memory, size_t size) {
	UNUSED(size);
	free(memory);
}
