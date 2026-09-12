/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_CHEAT_SPLIT_H
#define LIBRETRO_CHEAT_SPLIT_H

#include <stddef.h>

/* Longest `+`-separated segment kept intact while pairing an address with its
 * value. Segments in libretro `.cht` files are a handful of hex digits; this is
 * only a bound so the splitter never grows an unbounded buffer.
 */
#define MAX_CHEAT_SEGMENT_LENGTH 64

/* Copies the next line of a libretro GBA cheat string into `line` and returns
 * where the following one starts, or NULL once the string is exhausted.
 *
 * Segments are separated by `+`, the libretro convention, but a `.cht` file
 * splits one cheat line across two of them -- "320375D4+0000" for CodeBreaker,
 * "D8BAE4D9+4864DCE5" for GameShark -- so an eight-digit segment is joined to
 * the four- or eight-digit segment that follows it. Whitespace inside a segment
 * is kept as a single space, empty segments are skipped, and a line longer than
 * `size - 1` is truncated.
 */
const char* retroCheatNextLine(const char* code, char* line, size_t size);

#endif
