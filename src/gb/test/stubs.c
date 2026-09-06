/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GBMBCInit() references every MBC handler, so linking mbc.c pulls in logging,
// timing and memory mapping too. Nothing under test calls any of these; they
// exist to satisfy the linker and keep the test's link closure small.

#include <mgba-util/common.h>

#include <stdlib.h>

struct mTiming;
struct VFile;

void mLog(int category, int level, const char* format, ...) {
	(void) category;
	(void) level;
	(void) format;
}

int mLogGenerateCategory(const char* name, const char* id) {
	(void) name;
	(void) id;
	return 0;
}

int32_t mTimingCurrentTime(const struct mTiming* timing) {
	(void) timing;
	return 0;
}

uint32_t mColorConvert(uint32_t color, int from, int to) {
	(void) from;
	(void) to;
	return color;
}

size_t mCoreCallbacksListSize(const void* list) {
	(void) list;
	return 0;
}

void* mCoreCallbacksListGetConstPointer(const void* list, size_t index) {
	(void) list;
	(void) index;
	return NULL;
}

char* VFileReadline(struct VFile* vf, char* buffer, size_t size) {
	(void) vf;
	(void) buffer;
	(void) size;
	return NULL;
}

void* anonymousMemoryMap(size_t size) {
	return calloc(1, size);
}

void mappedMemoryFree(void* memory, size_t size) {
	(void) size;
	free(memory);
}
