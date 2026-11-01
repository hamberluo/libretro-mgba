/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "realtime-bgm.h"
#include "realtime-bgm-engine.h"

#include "aux-core.h"

#include <mgba/core/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HANDBACK_WAIT_FRAMES 180 // 3 s
#define CROSSFADE_FRAMES 12      // 200 ms
#define LANE_FRAMES 0x1000

enum State { IDLE, ACTIVE, HANDING_BACK, FELL_BACK };

struct RealtimeBgm {
	struct mCore* main;
	const void* rom;
	size_t romSize;
	struct RealtimeBgmEngine engine;

	struct RealtimeBgmShadow bgm;
	struct RealtimeBgmShadow sfx;

	enum State state;
	enum retro_bgm_status status;
	double speed;
	bool enabled;
	unsigned waitFrames;
	bool fading;         // handing back, the engine settled
	unsigned fadeFrames; // hand-back progress: rises while fading, falls back when active

	struct mAudioResampler bgmResampler;
	struct mAudioBuffer bgmOut;
	struct mAudioBuffer staging;
	int16_t mixA[LANE_FRAMES * 2];
	int16_t mixB[LANE_FRAMES * 2];
};

static unsigned liveShadows;
static unsigned clones;

static bool shadowCreate(struct RealtimeBgm* b, struct RealtimeBgmShadow* s) {
	s->core = mCoreCreate(b->main->platform(b->main));
	if (!s->core) {
		return false;
	}
	mCoreInitConfig(s->core, NULL);
	s->core->init(s->core);
	RetroAuxCoreConfigure(s->core);
	struct VFile* rom = VFileFromConstMemory(b->rom, b->romSize);
	if (!rom || !s->core->loadROM(s->core, rom)) {
		RetroAuxCoreDestroy(s->core);
		s->core = NULL;
		return false;
	}
	// mGBA refuses a state saved under a real BIOS while the PC is inside it
	// (a game waiting for VBlank) unless the loading core runs the same BIOS.
	struct GBA* mainGba = b->main->board;
	if (b->main->platform(b->main) == mPLATFORM_GBA && mainGba->memory.fullBios) {
		s->core->loadBIOS(s->core, VFileFromConstMemory(mainGba->memory.bios, GBA_SIZE_BIOS), 0);
	}
	// Before reset: that is when a core picks its renderer.
	s->video = calloc(256 * 224, sizeof(mColor));
	s->core->setVideoBuffer(s->core, s->video, 256);
	s->core->reset(s->core);
	RetroAuxCoreNeverDraw(s->core);
	++liveShadows;
	return true;
}

static void shadowDestroy(struct RealtimeBgmShadow* s) {
	if (s->core) {
		RetroAuxCoreDestroy(s->core);
		--liveShadows;
	}
	free(s->video);
	s->core = NULL;
	s->video = NULL;
}

static bool shadowClone(struct RealtimeBgm* b, struct RealtimeBgmShadow* s) {
	size_t size = b->main->stateSize(b->main);
	void* state = malloc(size);
	b->main->saveState(b->main, state);
	bool loaded = s->core->loadState(s->core, state);
	free(state);
	if (!loaded) {
		return false;
	}
	b->engine.cloned(b->engine.state, s);
	mAudioBufferClear(s->core->getAudioBuffer(s->core));
	return true;
}

static void fallBack(struct RealtimeBgm* b, enum retro_bgm_status why) {
	b->status = why;
	b->state = FELL_BACK;
	shadowDestroy(&b->bgm);
	shadowDestroy(&b->sfx);
	mAudioBufferClear(&b->bgmOut);
}

// False after falling back: a shadow that kept its own state plays nothing.
static bool cloneShadows(struct RealtimeBgm* b) {
	if (!shadowClone(b, &b->bgm) || !shadowClone(b, &b->sfx)) {
		fallBack(b, RETRO_BGM_CLONE_FAILED);
		return false;
	}
	mAudioBufferClear(&b->bgmOut);
	++clones;
	return true;
}

// False to retry on a later frame, or after falling back.
static bool engineReady(struct RealtimeBgm* b) {
	enum retro_bgm_status fail = RETRO_BGM_OK;
	if (b->engine.ready(b->engine.state, &fail)) {
		return true;
	}
	if (fail != RETRO_BGM_OK) {
		fallBack(b, fail);
	}
	return false;
}

static void takeOver(struct RealtimeBgm* b) {
	if (!engineReady(b)) {
		return;
	}
	if (!b->bgm.core || !b->sfx.core) {
		fallBack(b, RETRO_BGM_CLONE_FAILED);
		return;
	}
	// Untouched: each shadow's first sync stops what is not its own.
	if (cloneShadows(b)) {
		b->state = ACTIVE;
	}
}

static void finishHandBack(struct RealtimeBgm* b) {
	b->state = IDLE;
	b->fading = false;
	b->fadeFrames = 0;
	mAudioBufferClear(&b->bgmOut);
	mAudioBufferClear(&b->staging);
}

static double fadeAngle(const struct RealtimeBgm* b) {
	return M_PI / 2 * b->fadeFrames / CROSSFADE_FRAMES;
}

struct RealtimeBgm* RealtimeBgmCreate(struct mCore* main, const void* rom, size_t romSize,
                                      const struct RealtimeBgmEngine* engine, unsigned outRate) {
	struct RealtimeBgm* b = calloc(1, sizeof(*b));
	if (!b) {
		engine->destroy(engine->state);
		return NULL;
	}
	b->main = main;
	b->rom = rom;
	b->romSize = romSize;
	b->engine = *engine;
	b->status = RETRO_BGM_OK;
	b->speed = 1;
	mAudioBufferInit(&b->bgmOut, LANE_FRAMES * 2, 2);
	mAudioBufferInit(&b->staging, LANE_FRAMES, 2);
	mAudioResamplerInit(&b->bgmResampler, mINTERPOLATOR_SINC);
	mAudioResamplerSetDestination(&b->bgmResampler, &b->bgmOut, outRate);
	// Here rather than on the first press, where building them (the ROM's
	// CRC32 above all) stalled a phone for ~100 ms. A failure falls back then.
	shadowCreate(b, &b->bgm);
	shadowCreate(b, &b->sfx);
	b->sfx.sfx = true;
	return b;
}

void RealtimeBgmDestroy(struct RealtimeBgm* b) {
	if (!b) {
		return;
	}
	shadowDestroy(&b->bgm);
	shadowDestroy(&b->sfx);
	mAudioResamplerDeinit(&b->bgmResampler);
	mAudioBufferDeinit(&b->bgmOut);
	mAudioBufferDeinit(&b->staging);
	b->engine.destroy(b->engine.state);
	free(b);
}

void RealtimeBgmSetSpeed(struct RealtimeBgm* b, double speed, bool enabled) {
	b->speed = speed;
	b->enabled = enabled;
	bool wanted = speed > 1 && enabled;
	switch (b->state) {
	case IDLE:
		if (wanted) {
			takeOver(b);
		}
		break;
	case ACTIVE:
		if (!wanted) {
			b->state = HANDING_BACK;
			b->waitFrames = 0;
			b->fading = false;
		}
		break;
	case HANDING_BACK:
		// Recloning now would inherit any compensation main is still undoing.
		if (wanted) {
			b->state = ACTIVE;
			b->fading = false;
		}
		break;
	case FELL_BACK:
		break;
	}
}

void RealtimeBgmAfterMainFrame(struct RealtimeBgm* b) {
	b->engine.observe(b->engine.state);
	// Held since before m4aSoundInit (the boot logo): no press is coming.
	if (b->state == IDLE && b->enabled && b->speed > 1) {
		takeOver(b);
	}
	if (b->state != ACTIVE && b->state != HANDING_BACK) {
		return;
	}
	if (b->state == ACTIVE && b->speed > RETRO_BGM_SFX_SPEED_MAX) {
		return;
	}
	if (!b->engine.frame(b->engine.state, &b->sfx)) {
		fallBack(b, RETRO_BGM_BUDGET);
	}
}

void RealtimeBgmRun(struct RealtimeBgm* b, unsigned frames) {
	for (unsigned f = 0; f < frames && (b->state == ACTIVE || b->state == HANDING_BACK); ++f) {
		if (!b->engine.frame(b->engine.state, &b->bgm)) {
			fallBack(b, RETRO_BGM_BUDGET);
			return;
		}
		struct mAudioBuffer* out = b->bgm.core->getAudioBuffer(b->bgm.core);
		mAudioResamplerSetSource(&b->bgmResampler, out, b->bgm.core->audioSampleRate(b->bgm.core), true);
		mAudioResamplerProcess(&b->bgmResampler);
		if (b->state == ACTIVE) {
			// Pressed again mid fade: ramp back rather than snap (a click).
			if (b->fadeFrames > 0) {
				--b->fadeFrames;
			}
		} else if (!b->fading) {
			b->fading = b->engine.settled(b->engine.state, &b->bgm, &b->sfx) || ++b->waitFrames >= HANDBACK_WAIT_FRAMES;
		} else if (++b->fadeFrames >= CROSSFADE_FRAMES) {
			finishHandBack(b);
		}
	}
}

size_t RealtimeBgmRead(struct RealtimeBgm* b, int16_t* out, size_t maxFrames) {
	if (b->state != ACTIVE && b->state != HANDING_BACK) {
		mAudioBufferClear(&b->bgmOut);
		return 0;
	}
	size_t n = mAudioBufferRead(&b->bgmOut, out, maxFrames);
	if (b->fadeFrames > 0) {
		double gain = cos(fadeAngle(b));
		for (size_t i = 0; i < n * 2; ++i) {
			out[i] = (int16_t) (out[i] * gain);
		}
	}
	return n;
}

static struct mAudioBuffer* silence(struct RealtimeBgm* b, struct mAudioBuffer* mainBuffer) {
	size_t n = mAudioBufferAvailable(mainBuffer);
	mAudioBufferClear(mainBuffer);
	if (n > LANE_FRAMES) {
		n = LANE_FRAMES;
	}
	memset(b->mixA, 0, n * 4);
	mAudioBufferWrite(&b->staging, b->mixA, n);
	return &b->staging;
}

struct mAudioBuffer* RealtimeBgmGameAudio(struct RealtimeBgm* b, struct mAudioBuffer* mainBuffer) {
	bool compressed = b->enabled && b->speed > RETRO_BGM_SFX_SPEED_MAX;
	switch (b->state) {
	case IDLE:
	case FELL_BACK:
		// Dart skips the forced mute on the load-time status: before sound init,
		// or after a fall back, compressed audio is still noise.
		return compressed ? silence(b, mainBuffer) : mainBuffer;
	case ACTIVE:
		if (compressed) {
			return silence(b, mainBuffer);
		}
		// fall through: a fade may still be ramping back
	case HANDING_BACK:
		if (b->fadeFrames > 0) {
			// Main sets the length: a leftover would pile up for the whole fade.
			struct mAudioBuffer* fx = b->sfx.core->getAudioBuffer(b->sfx.core);
			size_t n = mAudioBufferAvailable(mainBuffer);
			if (n > LANE_FRAMES) {
				n = LANE_FRAMES;
			}
			mAudioBufferRead(mainBuffer, b->mixA, n);
			mAudioBufferClear(mainBuffer);
			size_t got = mAudioBufferRead(fx, b->mixB, n);
			memset(b->mixB + got * 2, 0, (n - got) * 4);
			// Equal power against the uncorrelated music lane; the effects in main
			// and in the shadow are in phase, so theirs sum to one.
			double mainGain = sin(fadeAngle(b)), shadowGain = 1 - mainGain;
			for (size_t i = 0; i < n * 2; ++i) {
				b->mixA[i] = (int16_t) (b->mixA[i] * mainGain + b->mixB[i] * shadowGain);
			}
			mAudioBufferWrite(&b->staging, b->mixA, n);
			return &b->staging;
		}
		break;
	}
	mAudioBufferClear(mainBuffer);
	return b->sfx.core->getAudioBuffer(b->sfx.core);
}

bool RealtimeBgmActive(const struct RealtimeBgm* b) {
	return b->state == ACTIVE || b->state == HANDING_BACK;
}

enum retro_bgm_status RealtimeBgmStatus(const struct RealtimeBgm* b) {
	return b->status;
}

void RealtimeBgmMainJumped(struct RealtimeBgm* b) {
	if (!RealtimeBgmActive(b)) {
		return;
	}
	// A reset lands before the game starts its driver: none would ever start on a clone.
	if (engineReady(b)) {
		cloneShadows(b);
	} else if (b->state != FELL_BACK) {
		finishHandBack(b);
	}
}

unsigned RealtimeBgmClonesForTesting(void) {
	return clones;
}

struct mCore* RealtimeBgmShadowForTesting(struct RealtimeBgm* b, bool sfx) {
	return sfx ? b->sfx.core : b->bgm.core;
}

unsigned RealtimeBgmLiveShadowsForTesting(void) {
	return liveShadows;
}
