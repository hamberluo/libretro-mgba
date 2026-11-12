/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A ROM whose size is not a power of two is copied into a padded buffer; the
// file mapping it was copied from must be unmapped, not left for the process
// (one ROM-sized mapping per game played).

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;
static int maps, unmaps;

struct CountingVFile {
	struct VFile d;
	struct VFile* inner;
};

static bool _close(struct VFile* vf) {
	struct VFile* inner = ((struct CountingVFile*) vf)->inner;
	free(vf);
	return inner->close(inner);
}
static off_t _seek(struct VFile* vf, off_t offset, int whence) {
	struct VFile* inner = ((struct CountingVFile*) vf)->inner;
	return inner->seek(inner, offset, whence);
}
static ssize_t _read(struct VFile* vf, void* buffer, size_t size) {
	struct VFile* inner = ((struct CountingVFile*) vf)->inner;
	return inner->read(inner, buffer, size);
}
static void* _map(struct VFile* vf, size_t size, int flags) {
	struct VFile* inner = ((struct CountingVFile*) vf)->inner;
	++maps;
	return inner->map(inner, size, flags);
}
static void _unmap(struct VFile* vf, void* memory, size_t size) {
	struct VFile* inner = ((struct CountingVFile*) vf)->inner;
	++unmaps;
	inner->unmap(inner, memory, size);
}
static ssize_t _size(struct VFile* vf) {
	struct VFile* inner = ((struct CountingVFile*) vf)->inner;
	return inner->size(inner);
}

static struct VFile* counting(struct VFile* inner) {
	struct CountingVFile* vf = calloc(1, sizeof(*vf));
	vf->inner = inner;
	vf->d.close = _close;
	vf->d.seek = _seek;
	vf->d.read = _read;
	vf->d.readline = inner->readline;
	vf->d.map = _map;
	vf->d.unmap = _unmap;
	vf->d.size = _size;
	return &vf->d;
}

static void playOnce(const char* name, uint8_t* rom, size_t size) {
	maps = unmaps = 0;
	struct mCore* core = GBACoreCreate();
	core->init(core);
	mCoreInitConfig(core, NULL);
	core->loadROM(core, counting(VFileFromConstMemory(rom, size)));
	core->reset(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	if (maps != 1 || unmaps != maps) {
		printf("FAIL: %s: %d maps, %d unmaps\n", name, maps, unmaps);
		++failures;
	}
}

int main(void) {
	static uint8_t rom[0x80100];
	playOnce("power of two", rom, 0x80000);
	playOnce("padded", rom, sizeof(rom));
	printf("rom-padded: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
