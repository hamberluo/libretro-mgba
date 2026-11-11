/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// GXA renders its channels in emulated time: pitch, length, loop, fade, gain,
// silence while inaudible, and a mid-play output rate change.

#include <mgba/internal/gba/gxa.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond); ++failures; } } while (0)
#define NEAR(got, want, tol) do { double g_ = (got), w_ = (want); if (fabs(g_ - w_) > (tol)) { printf("FAIL %s:%d: %s = %f, want %f\n", __func__, __LINE__, #got, g_, w_); ++failures; } } while (0)

#define CH(n, off) (0x10 + 0x10 * (n) + (off))
enum { TRACK = 0, CTRL = 2, VOL = 4, FADE = 6, STATE = 8, POS = 0xC };
enum { S_PLAYING = 1, S_ENDED = 4 };
#define RATE 32768
#define FULL (0.5 / sqrt(2) * 32767)

static struct GXA* create(void) {
	char path[1024];
	const char* slash = strrchr(__FILE__, '/');
	snprintf(path, sizeof(path), "%.*s/gxa/fixture.gxa", (int) (slash - __FILE__), __FILE__);
	struct GXA* gxa = GXACreate();
	GXASetPack(gxa, GXAPackOpen(VFileOpen(path, O_RDONLY)));
	return gxa;
}

static void play(struct GXA* gxa, int ch, int track, int ctrl) {
	GXAWrite16(gxa, CH(ch, TRACK), track);
	GXAWrite16(gxa, CH(ch, CTRL), ctrl);
}

// Produces n frames at rate in blocks, draining the ring into out (left channel).
static void run(struct GXA* gxa, int16_t* out, size_t n, unsigned rate) {
	int16_t buf[2 * 256];
	size_t done = 0;
	while (done < n) {
		size_t step = n - done < 256 ? n - done : 256;
		GXAProduce(gxa, step, rate);
		size_t got = GXARead(gxa, buf, step);
		size_t i;
		for (i = 0; i < got; ++i) {
			if (out) {
				out[done + i] = buf[i * 2];
			}
		}
		done += step;
	}
}

static double rms(const int16_t* x, size_t n) {
	double s = 0;
	size_t i;
	for (i = 0; i < n; ++i) {
		s += (double) x[i] * x[i];
	}
	return sqrt(s / n);
}

static double freq(const int16_t* x, size_t n, unsigned rate) {
	size_t i, crossings = 0;
	for (i = 1; i < n; ++i) {
		crossings += (x[i - 1] < 0) != (x[i] < 0);
	}
	return crossings / 2.0 * rate / n;
}

static double goertzel(const int16_t* x, size_t n, double f, unsigned rate) {
	double w = 2 * M_PI * f / rate, c = 2 * cos(w), s1 = 0, s2 = 0;
	size_t i;
	for (i = 0; i < n; ++i) {
		double s = x[i] + c * s1 - s2;
		s2 = s1;
		s1 = s;
	}
	return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static int16_t out[RATE * 3];

static void gxa_render_tone(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 1, 1);
	run(gxa, out, RATE / 2, RATE);
	NEAR(rms(&out[1024], RATE / 2 - 1024), FULL, FULL * 0.05);
	NEAR(freq(&out[1024], RATE / 2 - 1024, RATE), 440, 440 * 0.02);
	GXADestroy(gxa);
}

static void gxa_render_end(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 1, 1);
	run(gxa, NULL, RATE - 1, RATE);
	CHECK(GXARead16(gxa, CH(0, STATE)) == S_PLAYING);
	run(gxa, NULL, 1 + 64, RATE);
	CHECK(GXARead16(gxa, CH(0, STATE)) == S_ENDED);
	NEAR(GXARead16(gxa, CH(0, POS)), 1000, 2);
	GXADestroy(gxa);
}

static void gxa_render_loop(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 2, 2);
	run(gxa, out, RATE * 2, RATE);
	CHECK(GXARead16(gxa, CH(0, STATE)) == S_PLAYING);
	// source frames 12000..24000 (48 kHz): output 0.25..0.5 s in the first pass, 0.5..0.75 s after the loop
	size_t a = RATE / 4 + 256, b = RATE / 2 + 256, n = RATE / 4 - 512, i;
	double ab = 0, aa = 0, bb = 0;
	for (i = 0; i < n; ++i) {
		ab += (double) out[a + i] * out[b + i];
		aa += (double) out[a + i] * out[a + i];
		bb += (double) out[b + i] * out[b + i];
	}
	CHECK(ab / sqrt(aa * bb) > 0.99);
	GXADestroy(gxa);
}

static void gxa_render_fade(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 1, 1);
	run(gxa, NULL, 1024, RATE);
	GXAWrite16(gxa, CH(0, FADE), 100);
	GXAWrite16(gxa, CH(0, VOL), 0);
	run(gxa, out, RATE / 10 + RATE / 100 + 64, RATE);
	CHECK(GXARead16(gxa, CH(0, STATE)) == S_ENDED);
	// linear: the fade's last 10 ms average ~6% of full, the 10 ms after it are silent
	CHECK(rms(&out[RATE / 10 - RATE / 100], RATE / 100) < FULL * 0.1);
	CHECK(rms(&out[RATE / 10 + 64], RATE / 100) < FULL * 0.02);
	CHECK(rms(&out[0], RATE / 100) > FULL * 0.8);
	GXADestroy(gxa);
}

static void gxa_render_gain(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 3, 1);
	run(gxa, out, RATE / 5, RATE);
	NEAR(rms(&out[1024], RATE / 5 - 1024), FULL * 0.501, FULL * 0.501 * 0.03);
	GXADestroy(gxa);
}

static void gxa_render_inaudible(void) {
	struct GXA* gxa = create();
	GXASetAudible(gxa, false);
	play(gxa, 0, 1, 1);
	run(gxa, out, RATE - 1, RATE);
	CHECK(GXARead16(gxa, CH(0, STATE)) == S_PLAYING);
	CHECK(rms(out, RATE - 1) == 0);
	run(gxa, NULL, 1 + 64, RATE);
	CHECK(GXARead16(gxa, CH(0, STATE)) == S_ENDED);
	GXADestroy(gxa);
}

static void gxa_render_rate_change(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 1, 1);
	run(gxa, out, RATE / 4, RATE);
	NEAR(freq(&out[512], RATE / 4 - 512, RATE), 440, 440 * 0.02);
	run(gxa, out, RATE / 2, RATE * 2);
	NEAR(freq(&out[512], RATE / 2 - 512, RATE * 2), 440, 440 * 0.02);
	GXADestroy(gxa);
}

static void gxa_render_mixed_formats(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 1, 1);
	play(gxa, 1, 2, 1);
	int16_t buf[64];
	int end0 = -1, end1 = -1, t;
	for (t = 0; t < RATE * 11 / 10; t += 32) {
		GXAProduce(gxa, 32, RATE);
		size_t got = GXARead(gxa, buf, 32), i;
		for (i = 0; i < got && t + i < sizeof(out) / sizeof(*out); ++i) {
			out[t + i] = buf[i * 2];
		}
		if (end0 < 0 && GXARead16(gxa, CH(0, STATE)) & S_ENDED) {
			end0 = t + 32;
		}
		if (end1 < 0 && GXARead16(gxa, CH(1, STATE)) & S_ENDED) {
			end1 = t + 32;
		}
	}
	size_t n = RATE / 4;
	CHECK(goertzel(&out[1024], n, 440, RATE) > 4 * goertzel(&out[1024], n, 660, RATE));
	CHECK(goertzel(&out[1024], n, 880, RATE) > 4 * goertzel(&out[1024], n, 660, RATE));
	NEAR(end1, RATE / 2, RATE * 3 / 1000 + 32);
	NEAR(end0, RATE, RATE * 3 / 1000 + 32);
	GXADestroy(gxa);
}

// GXASkip keeps only the newest frames: a backlog the mix could not take is dropped, not played late
static void gxa_render_skip(void) {
	struct GXA* gxa = create();
	play(gxa, 0, 1, 1);
	GXAProduce(gxa, 3000, RATE);
	GXASkip(gxa, 1000);
	CHECK(GXAAvailable(gxa) == 1000);
	int16_t a[2000 * 2], b[2];
	CHECK(GXARead(gxa, a, 2000) == 1000);
	struct GXA* ref = create();
	play(ref, 0, 1, 1);
	run(ref, out, 3000, RATE);
	size_t i;
	int same = 1;
	for (i = 0; i < 1000; ++i) {
		same &= a[i * 2] == out[2000 + i];
	}
	CHECK(same);
	CHECK(GXARead(gxa, b, 1) == 0);
	GXADestroy(ref);
	GXADestroy(gxa);
}

int main(void) {
	gxa_render_tone();
	gxa_render_end();
	gxa_render_loop();
	gxa_render_fade();
	gxa_render_gain();
	gxa_render_inaudible();
	gxa_render_rate_change();
	gxa_render_mixed_formats();
	gxa_render_skip();
	printf("gxa-render: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
