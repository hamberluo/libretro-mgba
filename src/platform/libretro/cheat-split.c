/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "cheat-split.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Copies one `+`-separated segment into `buffer`, returning where the next one
// starts or NULL once the string is exhausted. Empty segments are skipped.
static const char* nextSegment(const char* code, char* buffer, size_t size, size_t* length) {
	for (;;) {
		if (!*code) {
			return NULL;
		}
		size_t pos = 0;
		while (*code && *code != '+') {
			char c = *code++;
			if (pos + 1 < size) {
				buffer[pos++] = isspace((int) c) ? ' ' : c;
			}
		}
		buffer[pos] = '\0';
		if (*code == '+') {
			++code;
		}
		if (pos) {
			*length = pos;
			return code;
		}
	}
}

// True for a segment of exactly `want` hex digits and nothing else.
static bool isHexRun(const char* segment, size_t length, size_t want) {
	if (length != want) {
		return false;
	}
	size_t i;
	for (i = 0; i < want; ++i) {
		if (!isxdigit((int) segment[i])) {
			return false;
		}
	}
	return true;
}

const char* retroCheatNextLine(const char* code, char* line, size_t size) {
	char first[MAX_CHEAT_SEGMENT_LENGTH];
	size_t firstLength = 0;
	const char* afterFirst = nextSegment(code, first, sizeof(first), &firstLength);
	if (!afterFirst) {
		return NULL;
	}

	// A cheat line is an address and a value, but libretro `.cht` files split
	// the two across `+`: CodeBreaker writes "320375D4+0000" and GameShark
	// writes "D8BAE4D9+4864DCE5" for what the parsers want as one line. An
	// eight-digit segment is therefore an address awaiting its value, never a
	// line of its own -- on its own it parses as nothing at all. Segments the
	// frontend already stored whole (a 16-digit PARv3 line, say) have no
	// pairing to do and pass straight through.
	if (isHexRun(first, firstLength, 8)) {
		char second[MAX_CHEAT_SEGMENT_LENGTH];
		size_t secondLength = 0;
		const char* afterSecond = nextSegment(afterFirst, second, sizeof(second), &secondLength);
		if (afterSecond && (isHexRun(second, secondLength, 4) || isHexRun(second, secondLength, 8))) {
			snprintf(line, size, "%s %s", first, second);
			return afterSecond;
		}
	}

	strncpy(line, first, size - 1);
	line[size - 1] = '\0';
	return afterFirst;
}
