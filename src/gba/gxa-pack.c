/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/gxa.h>

#include <mgba-util/sha1.h>
#include <mgba-util/vfs.h>

// cJSON is compiled here, so builds that take src/gba/*.c need no extra source
#include "third-party/cjson/cJSON.c"

#define GXA_MAX_MATCH 16

struct GXATrack {
	uint16_t id;
	int32_t loop;
	float gainDb;
	const uint8_t* data;   // NULL: listed, but its file is not in the zip
	size_t size;
};

struct GXAMatchRule {
	bool sha1;
	uint8_t digest[20];
	char code[4];
	char title[13];
};

struct GXAPack {
	struct VFile* vf;
	uint8_t* map;
	size_t mapSize;
	uint8_t id[20];
	char title[64];
	char version[32];
	struct GXAMatchRule match[GXA_MAX_MATCH];
	size_t nMatch;
	struct GXATrack* tracks;   // sorted by id
	size_t nTracks;
};

struct GXAEntry {
	const char* name;
	size_t nameLen;
	const uint8_t* data;
	size_t size;
};

static uint16_t _le16(const uint8_t* p) {
	return p[0] | (p[1] << 8);
}

static uint32_t _le32(const uint8_t* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

// offset + len <= size, without the sum wrapping on 32-bit hosts (packs are untrusted)
static bool _fits(size_t offset, size_t len, size_t size) {
	return offset <= size && len <= size - offset;
}

// The central directory's entries; false on anything but a plain store-only zip.
static bool _readZip(const uint8_t* z, size_t size, struct GXAEntry** out, size_t* count) {
	if (size < 22) {
		return false;
	}
	size_t eocd = size - 22;
	size_t floor = size > 22 + 0xFFFF ? size - 22 - 0xFFFF : 0;
	while (_le32(&z[eocd]) != 0x06054B50) {
		if (eocd == floor) {
			return false;
		}
		--eocd;
	}
	size_t n = _le16(&z[eocd + 10]);
	size_t cd = _le32(&z[eocd + 16]);
	struct GXAEntry* entries = calloc(n ? n : 1, sizeof(*entries));
	size_t i;
	for (i = 0; i < n; ++i) {
		if (!_fits(cd, 46, size) || _le32(&z[cd]) != 0x02014B50) {
			break;
		}
		uint16_t method = _le16(&z[cd + 10]);
		uint32_t csize = _le32(&z[cd + 20]);
		uint32_t usize = _le32(&z[cd + 24]);
		size_t nameLen = _le16(&z[cd + 28]);
		size_t skip = 46 + nameLen + _le16(&z[cd + 30]) + _le16(&z[cd + 32]);
		size_t local = _le32(&z[cd + 42]);
		if (method != 0 || csize != usize || !_fits(cd, skip, size) || !_fits(local, 30, size) ||
		    _le32(&z[local]) != 0x04034B50) {
			break;
		}
		size_t data = local + 30 + _le16(&z[local + 26]) + _le16(&z[local + 28]);   // <= size + 2 * 0xFFFF
		if (!_fits(data, csize, size)) {
			break;
		}
		entries[i] = (struct GXAEntry) { (const char*) &z[cd + 46], nameLen, &z[data], csize };
		cd += skip;
	}
	if (i != n) {
		free(entries);
		return false;
	}
	*out = entries;
	*count = n;
	return true;
}

static const struct GXAEntry* _find(const struct GXAEntry* e, size_t n, const char* name) {
	size_t len = strlen(name);
	size_t i;
	for (i = 0; i < n; ++i) {
		if (e[i].nameLen == len && !memcmp(e[i].name, name, len)) {
			return &e[i];
		}
	}
	return NULL;
}

static int _byId(const void* a, const void* b) {
	return (int) ((const struct GXATrack*) a)->id - (int) ((const struct GXATrack*) b)->id;
}

static bool _hex(const char* s, uint8_t out[20]) {
	if (!s || strlen(s) != 40) {
		return false;
	}
	size_t i;
	for (i = 0; i < 20; ++i) {
		unsigned v;
		if (sscanf(&s[i * 2], "%2x", &v) != 1) {
			return false;
		}
		out[i] = v;
	}
	return true;
}

static bool _manifest(struct GXAPack* pack, const struct GXAEntry* m, const struct GXAEntry* e, size_t n) {
	cJSON* root = cJSON_ParseWithLength((const char*) m->data, m->size);
	if (!root) {
		return false;
	}
	bool ok = false;
	const cJSON* spec = cJSON_GetObjectItemCaseSensitive(root, "spec");
	const cJSON* match = cJSON_GetObjectItemCaseSensitive(root, "match");
	const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
	if (!cJSON_IsNumber(spec) || spec->valueint != 1 || !cJSON_IsArray(match) || !cJSON_IsObject(tracks)) {
		goto done;
	}
	const cJSON* s = cJSON_GetObjectItemCaseSensitive(root, "title");
	if (cJSON_IsString(s)) {
		strlcpy(pack->title, s->valuestring, sizeof(pack->title));
	}
	s = cJSON_GetObjectItemCaseSensitive(root, "version");
	if (cJSON_IsString(s)) {
		strlcpy(pack->version, s->valuestring, sizeof(pack->version));
	}
	const cJSON* rule;
	cJSON_ArrayForEach(rule, match) {
		if (pack->nMatch == GXA_MAX_MATCH) {
			break;
		}
		struct GXAMatchRule* r = &pack->match[pack->nMatch];
		const cJSON* sha = cJSON_GetObjectItemCaseSensitive(rule, "sha1");
		const cJSON* code = cJSON_GetObjectItemCaseSensitive(rule, "code");
		const cJSON* title = cJSON_GetObjectItemCaseSensitive(rule, "title");
		if (cJSON_IsString(sha)) {
			if (!_hex(sha->valuestring, r->digest)) {
				goto done;
			}
			r->sha1 = true;
		} else if (cJSON_IsString(code) && strlen(code->valuestring) == 4 && cJSON_IsString(title)) {
			memcpy(r->code, code->valuestring, 4);
			strlcpy(r->title, title->valuestring, sizeof(r->title));
		} else {
			goto done;
		}
		++pack->nMatch;
	}
	pack->nTracks = cJSON_GetArraySize(tracks);
	pack->tracks = calloc(pack->nTracks ? pack->nTracks : 1, sizeof(*pack->tracks));
	size_t i = 0;
	const cJSON* t;
	cJSON_ArrayForEach(t, tracks) {
		char* end;
		long id = strtol(t->string, &end, 10);
		const cJSON* file = cJSON_GetObjectItemCaseSensitive(t, "file");
		const cJSON* loop = cJSON_GetObjectItemCaseSensitive(t, "loop");
		const cJSON* gain = cJSON_GetObjectItemCaseSensitive(t, "gain");
		if (*end || end == t->string || id < 0 || id > 0xFFFF || !cJSON_IsString(file)) {
			goto done;
		}
		struct GXATrack* tr = &pack->tracks[i++];
		tr->id = id;
		tr->loop = cJSON_IsNumber(loop) ? loop->valueint : -1;
		tr->gainDb = cJSON_IsNumber(gain) ? (float) gain->valuedouble : 0;
		const struct GXAEntry* f = _find(e, n, file->valuestring);
		if (f) {
			tr->data = f->data;
			tr->size = f->size;
		}
	}
	qsort(pack->tracks, pack->nTracks, sizeof(*pack->tracks), _byId);
	ok = true;
done:
	cJSON_Delete(root);
	return ok;
}

struct GXAPack* GXAPackOpen(struct VFile* vf) {
	if (!vf) {
		return NULL;
	}
	struct GXAPack* pack = calloc(1, sizeof(*pack));
	pack->vf = vf;
	pack->mapSize = vf->size(vf);
	pack->map = pack->mapSize ? vf->map(vf, pack->mapSize, MAP_READ) : NULL;
	struct GXAEntry* entries = NULL;
	size_t n = 0;
	const struct GXAEntry* m;
	if (!pack->map || !_readZip(pack->map, pack->mapSize, &entries, &n) || !(m = _find(entries, n, "manifest.json")) ||
	    !_manifest(pack, m, entries, n)) {
		free(entries);
		GXAPackClose(pack);
		return NULL;
	}
	sha1Buffer(m->data, m->size, pack->id);
	free(entries);
	return pack;
}

void GXAPackClose(struct GXAPack* pack) {
	if (!pack) {
		return;
	}
	if (pack->map) {
		pack->vf->unmap(pack->vf, pack->map, pack->mapSize);
	}
	pack->vf->close(pack->vf);
	free(pack->tracks);
	free(pack);
}

enum GXAMatch GXAPackMatch(const struct GXAPack* pack, const void* rom, size_t size) {
	uint8_t digest[20];
	sha1Buffer(rom, size, digest);
	enum GXAMatch best = GXA_MATCH_NONE;
	size_t i;
	for (i = 0; i < pack->nMatch; ++i) {
		const struct GXAMatchRule* r = &pack->match[i];
		if (r->sha1) {
			if (!memcmp(r->digest, digest, 20)) {
				return GXA_MATCH_SHA1;
			}
		} else if (size >= 0xB0) {
			const char* h = rom;
			char title[13] = { 0 };
			memcpy(title, &h[0xA0], 12);
			size_t len = strlen(title);
			while (len && title[len - 1] == ' ') {
				title[--len] = '\0';
			}
			if (!memcmp(&h[0xAC], r->code, 4) && !strcmp(title, r->title)) {
				best = GXA_MATCH_HEADER;
			}
		}
	}
	return best;
}

bool GXAPackTrack(const struct GXAPack* pack, uint16_t id, const void** data, size_t* size, int32_t* loop, float* gainDb) {
	struct GXATrack key = { .id = id };
	const struct GXATrack* t = bsearch(&key, pack->tracks, pack->nTracks, sizeof(key), _byId);
	if (!t || !t->data) {
		return false;
	}
	*data = t->data;
	*size = t->size;
	*loop = t->loop;
	*gainDb = t->gainDb;
	return true;
}

void GXAPackId(const struct GXAPack* pack, uint8_t out[20]) {
	memcpy(out, pack->id, 20);
}

const char* GXAPackTitle(const struct GXAPack* pack) {
	return pack->title;
}

const char* GXAPackVersion(const struct GXAPack* pack) {
	return pack->version;
}
