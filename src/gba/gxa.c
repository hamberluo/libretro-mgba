/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/gxa.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "third-party/stb/stb_vorbis.c"

enum {
	REG_ID0 = 0x00,
	REG_ID1 = 0x02,
	REG_HANDSHAKE = 0x04,
	REG_VERSION = 0x06,
	REG_STATUS = 0x08,
	REG_GAMEVOL = 0x0A,
	REG_DUCK = 0x0C,
	REG_CHANNEL0 = 0x10,
};

enum {
	CH_TRACK = 0x0,
	CH_CTRL = 0x2,
	CH_VOL = 0x4,
	CH_FADE = 0x6,
	CH_STATE = 0x8,
	CH_POS_LO = 0xC,
	CH_POS_HI = 0xE,
};

enum {
	CTRL_PLAY = 1,
	CTRL_LOOP = 2,
	CTRL_STOP = 3,
	CTRL_PAUSE = 4,
	CTRL_RESUME = 5,
};

enum {
	STATE_PLAYING = 1,
	STATE_PAUSED = 2,
	STATE_ENDED = 4,
	STATE_MISSING = 8,
	STATE_ERROR = 16,
};

#define GXA_ID 0x5847
#define GXA_ID1 0x3141
#define GXA_VERSION 0x0100
#define GXA_UNITY 256
#define GXA_CACHE 1024   // decoded source frames per channel
#define GXA_RING 8192    // rendered output frames

struct GXAChannel {
	uint16_t track;
	uint16_t vol;
	uint16_t fade;
	uint16_t state;
	bool loop;
	stb_vorbis* dec;
	unsigned srcRate;
	int srcChannels;
	int32_t loopFrame;
	float gain;
	int64_t total;      // source frames
	double pos;         // source frame being played
	double elapsedMs;   // since the play: POS
	float cur;          // volume now (0-1), ramping to target over fadeLeftMs
	float target;
	double fadeLeftMs;
	int16_t cache[GXA_CACHE * 2];
	int64_t cacheStart;
	int cacheLen;
};

struct GXA {
	struct GXAPack* pack;
	uint16_t handshake;
	uint16_t gamevol;
	uint16_t duck;
	bool audible;
	struct GXAChannel ch[GXA_CHANNELS];
	int16_t ring[GXA_RING * 2];
	size_t ringRead;
	size_t ringCount;
};

static void _close(struct GXAChannel* c) {
	if (c->dec) {
		stb_vorbis_close(c->dec);
		c->dec = NULL;
	}
}

static void _stop(struct GXAChannel* c, uint16_t state) {
	_close(c);
	c->state = state;
}

static void _play(struct GXA* gxa, struct GXAChannel* c, bool loop) {
	_close(c);
	c->elapsedMs = 0;
	c->pos = 0;
	c->cacheLen = 0;
	c->loop = loop;
	const void* data;
	size_t size;
	int32_t loopFrame;
	float gainDb;
	if (!gxa->pack || !GXAPackTrack(gxa->pack, c->track, &data, &size, &loopFrame, &gainDb)) {
		c->state = STATE_MISSING | STATE_ENDED;
		return;
	}
	int err = 0;
	c->dec = stb_vorbis_open_memory(data, (int) size, &err, NULL);
	if (!c->dec) {
		c->state = STATE_ERROR | STATE_ENDED;
		return;
	}
	stb_vorbis_info info = stb_vorbis_get_info(c->dec);
	c->srcRate = info.sample_rate;
	c->srcChannels = info.channels;
	c->total = stb_vorbis_stream_length_in_samples(c->dec);
	c->loopFrame = loopFrame > 0 && loopFrame < c->total ? loopFrame : 0;
	c->gain = powf(10.f, gainDb / 20.f);
	c->cur = c->target = c->vol / (float) GXA_UNITY;
	c->fadeLeftMs = 0;
	if (!c->total || !c->srcRate) {
		_stop(c, STATE_ERROR | STATE_ENDED);
		return;
	}
	c->state = STATE_PLAYING;
}

static void _reset(struct GXA* gxa) {
	size_t i;
	for (i = 0; i < GXA_CHANNELS; ++i) {
		_close(&gxa->ch[i]);
		memset(&gxa->ch[i], 0, sizeof(gxa->ch[i]));
		gxa->ch[i].vol = GXA_UNITY;
	}
	gxa->gamevol = GXA_UNITY;
	gxa->duck = GXA_UNITY;
	gxa->handshake = 0;
	gxa->ringRead = 0;
	gxa->ringCount = 0;
}

struct GXA* GXACreate(void) {
	struct GXA* gxa = calloc(1, sizeof(*gxa));
	gxa->audible = true;
	_reset(gxa);
	return gxa;
}

void GXADestroy(struct GXA* gxa) {
	if (!gxa) {
		return;
	}
	size_t i;
	for (i = 0; i < GXA_CHANNELS; ++i) {
		_close(&gxa->ch[i]);
	}
	GXAPackClose(gxa->pack);
	free(gxa);
}

void GXASetPack(struct GXA* gxa, struct GXAPack* pack) {
	size_t i;
	for (i = 0; i < GXA_CHANNELS; ++i) {
		if (gxa->ch[i].state & (STATE_PLAYING | STATE_PAUSED)) {
			_stop(&gxa->ch[i], STATE_ENDED);
		}
	}
	if (gxa->pack != pack) {
		GXAPackClose(gxa->pack);
	}
	gxa->pack = pack;
}

struct GXAPack* GXAGetPack(const struct GXA* gxa) {
	return gxa->pack;
}

static uint32_t _posMs(const struct GXAChannel* c) {
	return (uint32_t) (c->elapsedMs + 0.5);
}

uint16_t GXARead16(struct GXA* gxa, uint32_t offset) {
	switch (offset) {
	case REG_ID0:
		return GXA_ID;
	case REG_ID1:
		return GXA_ID1;
	case REG_HANDSHAKE:
		return gxa->handshake ^ GXA_ID;
	case REG_VERSION:
		return GXA_VERSION;
	case REG_STATUS:
		return (gxa->pack ? 1 : 0) | (GXA_CHANNELS << 8);
	case REG_GAMEVOL:
		return gxa->gamevol;
	case REG_DUCK:
		return gxa->duck;
	}
	if (offset < REG_CHANNEL0) {
		return 0;
	}
	struct GXAChannel* c = &gxa->ch[(offset - REG_CHANNEL0) >> 4];
	switch (offset & 0xF) {
	case CH_TRACK:
		return c->track;
	case CH_VOL:
		return c->vol;
	case CH_FADE:
		return c->fade;
	case CH_STATE:
		return c->state;
	case CH_POS_LO:
		return _posMs(c);
	case CH_POS_HI:
		return _posMs(c) >> 16;
	}
	return 0;
}

static uint16_t _q8(uint16_t value) {
	return value > GXA_UNITY ? GXA_UNITY : value;
}

void GXAWrite16(struct GXA* gxa, uint32_t offset, uint16_t value) {
	switch (offset) {
	case REG_HANDSHAKE:
		gxa->handshake = value;
		return;
	case REG_GAMEVOL:
		gxa->gamevol = _q8(value);
		return;
	case REG_DUCK:
		gxa->duck = _q8(value);
		return;
	}
	if (offset < REG_CHANNEL0) {
		return;
	}
	struct GXAChannel* c = &gxa->ch[(offset - REG_CHANNEL0) >> 4];
	switch (offset & 0xF) {
	case CH_TRACK:
		c->track = value;
		break;
	case CH_VOL:
		c->vol = _q8(value);
		c->target = c->vol / (float) GXA_UNITY;
		c->fadeLeftMs = c->fade;
		if (!c->fade) {
			c->cur = c->target;
		}
		break;
	case CH_FADE:
		c->fade = value;
		break;
	case CH_CTRL:
		switch (value) {
		case CTRL_PLAY:
		case CTRL_LOOP:
			_play(gxa, c, value == CTRL_LOOP);
			break;
		case CTRL_STOP:
			_stop(c, 0);
			break;
		case CTRL_PAUSE:
			if (c->state & STATE_PLAYING) {
				c->state = (c->state & ~STATE_PLAYING) | STATE_PAUSED;
			}
			break;
		case CTRL_RESUME:
			if (c->state & STATE_PAUSED) {
				c->state = (c->state & ~STATE_PAUSED) | STATE_PLAYING;
			}
			break;
		}
		break;
	}
}

int32_t GXAGameGain(const struct GXA* gxa) {
	size_t i;
	for (i = 0; i < GXA_CHANNELS; ++i) {
		if (gxa->ch[i].state & STATE_PLAYING) {
			return gxa->gamevol * gxa->duck / GXA_UNITY;
		}
	}
	return gxa->gamevol;
}

// The stereo frame at source frame idx, decoding (or seeking) when it is not cached.
static const int16_t* _frame(struct GXAChannel* c, int64_t idx) {
	static const int16_t silence[2];
	if (idx < c->cacheStart || idx >= c->cacheStart + c->cacheLen) {
		if (idx != c->cacheStart + c->cacheLen || !c->cacheLen) {
			stb_vorbis_seek(c->dec, (unsigned) idx);
		}
		c->cacheStart = idx;
		c->cacheLen = stb_vorbis_get_samples_short_interleaved(c->dec, 2, c->cache, GXA_CACHE * 2);
		if (c->cacheLen <= 0) {
			c->cacheLen = 0;
			return silence;
		}
	}
	return &c->cache[(idx - c->cacheStart) * 2];
}

// One output frame of a playing channel: adds it to mix when audible, then advances.
static void _step(struct GXAChannel* c, float mix[2], bool audible, unsigned rate) {
	if (audible) {
		int64_t i = (int64_t) c->pos;
		float f = (float) (c->pos - i);
		int64_t j = i + 1 < c->total ? i + 1 : c->loop ? c->loopFrame : i;
		const int16_t* a = _frame(c, i);
		float a0 = a[0], a1 = a[1];
		const int16_t* b = _frame(c, j);
		float g = c->gain * c->cur;
		mix[0] += (a0 + (b[0] - a0) * f) * g;
		mix[1] += (a1 + (b[1] - a1) * f) * g;
	}
	double dt = 1000.0 / rate;
	c->elapsedMs += dt;
	if (c->fadeLeftMs > 0) {
		c->cur += (c->target - c->cur) * (float) (dt < c->fadeLeftMs ? dt / c->fadeLeftMs : 1);
		c->fadeLeftMs -= dt;
		if (c->fadeLeftMs <= 0) {
			c->fadeLeftMs = 0;
			c->cur = c->target;
			if (c->target == 0) {
				_stop(c, STATE_ENDED);
				return;
			}
		}
	}
	c->pos += (double) c->srcRate / rate;
	if (c->pos >= c->total) {
		if (!c->loop) {
			_stop(c, STATE_ENDED);
			return;
		}
		c->pos = c->loopFrame + fmod(c->pos - c->total, (double) (c->total - c->loopFrame));
	}
}

void GXAProduce(struct GXA* gxa, unsigned frames, unsigned rate) {
	if (!rate) {
		return;
	}
	unsigned n;
	for (n = 0; n < frames; ++n) {
		float mix[2] = { 0, 0 };
		size_t i;
		for (i = 0; i < GXA_CHANNELS; ++i) {
			if (gxa->ch[i].state & STATE_PLAYING) {
				_step(&gxa->ch[i], mix, gxa->audible, rate);
			}
		}
		if (gxa->ringCount == GXA_RING) {
			gxa->ringRead = (gxa->ringRead + 1) % GXA_RING;
			--gxa->ringCount;
		}
		size_t w = (gxa->ringRead + gxa->ringCount) % GXA_RING;
		gxa->ring[w * 2] = mix[0] > 32767 ? 32767 : mix[0] < -32768 ? -32768 : (int16_t) mix[0];
		gxa->ring[w * 2 + 1] = mix[1] > 32767 ? 32767 : mix[1] < -32768 ? -32768 : (int16_t) mix[1];
		++gxa->ringCount;
	}
}

size_t GXARead(struct GXA* gxa, int16_t* stereo, size_t maxFrames) {
	size_t n = 0;
	while (n < maxFrames && gxa->ringCount) {
		stereo[n * 2] = gxa->ring[gxa->ringRead * 2];
		stereo[n * 2 + 1] = gxa->ring[gxa->ringRead * 2 + 1];
		gxa->ringRead = (gxa->ringRead + 1) % GXA_RING;
		--gxa->ringCount;
		++n;
	}
	return n;
}

size_t GXAAvailable(const struct GXA* gxa) {
	return gxa->ringCount;
}

void GXASetAudible(struct GXA* gxa, bool audible) {
	gxa->audible = audible;
}
