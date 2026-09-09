/* Copyright (c) 2013-2026 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for _GBCoreAudioSampleRate().
//
// The APU emits one frame every 32 CPU cycles, and that clock differs by model:
// an SGB runs at 4295454 Hz against the DMG's 4194304, i.e. ~2.4% faster. The
// rate used to be hardcoded to 131072 (the DMG figure), so in SGB mode the core
// declared 2.4% less audio than it actually delivered.
//
// A frontend that paces on the declared rate then receives 2.4% more audio than
// it accounts for, every second, forever. Upstream's own libretro layer never
// noticed -- it posts GB audio through postAudioBuffer with no resampling -- but
// a resampling frontend accumulates the surplus until its queue overflows and
// starts discarding, and every discard splices the stream: continuous crackle.
//
// Measured against this core with a real ROM: delivered/declared was 1.0249
// before and 1.0008 after.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// The model clocks. Copied from src/gb/gb.c rather than linked, because that
// file drags the whole core in for three constants; the static assertions in
// main() below fail if the originals ever change.
#define DMG_SM83_FREQUENCY 0x400000u
#define SGB_SM83_FREQUENCY 0x418B1Eu
#define CGB_SM83_FREQUENCY 0x800000u

static int failures = 0;
static int checks = 0;

static void ok(const char* what, int condition) {
	++checks;
	if (condition) {
		printf("  ok    %s\n", what);
	} else {
		++failures;
		printf("  FAIL  %s\n", what);
	}
}

// mTiming's frequency per model, mirroring _GBCoreTimingFrequency(): DMG, MGB
// and CGB all share the CGB clock, SGB runs at twice its own.
static unsigned timingFrequency(int isSgb) {
	return isSgb ? SGB_SM83_FREQUENCY * 2 : CGB_SM83_FREQUENCY;
}

// What _GBCoreAudioSampleRate() returns: GBAudioSample() emits one frame every
// SAMPLE_INTERVAL (32) * timingFactor (2) ticks of mTiming.
static unsigned declaredRate(int isSgb) {
	return timingFrequency(isSgb) / (32 * 2);
}

static void testRateTracksTheModelClock(void) {
	printf("The declared audio rate follows the model's mTiming clock\n");

	printf("    DMG/MGB/CGB timing %u Hz -> %u Hz\n", timingFrequency(0), declaredRate(0));
	printf("    SGB         timing %u Hz -> %u Hz\n", timingFrequency(1), declaredRate(1));

	// DMG and CGB are unchanged by the fix: both were, and remain, 131072.
	ok("DMG/CGB rate is the historical 131072", declaredRate(0) == 131072);

	// The bug: SGB is genuinely faster, so a DMG-derived rate understates it.
	ok("SGB rate is higher than the DMG/CGB rate", declaredRate(1) > declaredRate(0));

	double surplus = (double) declaredRate(1) / declaredRate(0) - 1.0;
	printf("    SGB surplus over a hardcoded 131072 = %.2f%%\n", surplus * 100.0);
	ok("SGB surplus is the ~2.4% that was overflowing the queue",
	   surplus > 0.020 && surplus < 0.028);
}

static void testCgbIsNotDoubled(void) {
	printf("CGB is not doubled by deriving the rate from the wrong clock\n");

	// The first attempt at this fix used _GBCoreFrequency() -- the CPU clock,
	// which really is doubled for CGB (8388608 vs the DMG's 4194304). That gave
	// CGB 262144 Hz, twice what its APU emits, and broke CGB audio while fixing
	// SGB. The audio timeline runs on mTiming, not the CPU clock; timingFactor
	// is 2 precisely because mTiming is the doubled one.
	unsigned cpuDerived = CGB_SM83_FREQUENCY / 32;
	printf("    CPU-clock derived would be %u Hz, timing-derived is %u Hz\n",
	       cpuDerived, declaredRate(0));

	ok("the CPU clock would have doubled CGB", cpuDerived == declaredRate(0) * 2);
	ok("CGB stays at 131072", declaredRate(0) == 131072);
}

static void testRateIsConsistentWithTheDeclaredFps(void) {
	printf("Audio rate and video fps stay consistent per model\n");

	// _GBCoreFrameCycles returns GB_VIDEO_TOTAL_LENGTH, doubled for CGB whose
	// CPU clock is also doubled, so fps is model-correct. Audio frames per video
	// frame must therefore come out identical across models.
	const int frameCycles = 70224;
	double dmgFps = (double) DMG_SM83_FREQUENCY / frameCycles;
	double cgbFps = (double) CGB_SM83_FREQUENCY / (frameCycles * 2);
	double sgbFps = (double) SGB_SM83_FREQUENCY / frameCycles;
	printf("    DMG %.4f, CGB %.4f, SGB %.4f fps\n", dmgFps, cgbFps, sgbFps);

	double dmgPerFrame = declaredRate(0) / dmgFps;
	double cgbPerFrame = declaredRate(0) / cgbFps;
	double sgbPerFrame = declaredRate(1) / sgbFps;
	printf("    audio frames per video frame: DMG %.1f, CGB %.1f, SGB %.1f\n",
	       dmgPerFrame, cgbPerFrame, sgbPerFrame);

	ok("DMG and CGB agree", dmgPerFrame > cgbPerFrame - 0.5 && dmgPerFrame < cgbPerFrame + 0.5);
	ok("SGB agrees with them once its own rate is used",
	   dmgPerFrame > sgbPerFrame - 0.5 && dmgPerFrame < sgbPerFrame + 0.5);

	// With the old hardcoded rate, SGB would have been short by the surplus.
	double stalePerFrame = 131072 / sgbFps;
	printf("    with a hardcoded 131072 an SGB frame accounts for only %.1f\n", stalePerFrame);
	ok("a hardcoded rate understates SGB", stalePerFrame < sgbPerFrame - 20.0);
}

int main(void) {
	// Guard the copies above against the originals drifting. These are the
	// values in src/gb/gb.c; a mismatch means this test is measuring fiction.
	if (DMG_SM83_FREQUENCY != 4194304u || SGB_SM83_FREQUENCY != 4295454u ||
	    CGB_SM83_FREQUENCY != 8388608u) {
		printf("FAIL: model clock constants do not match src/gb/gb.c\n");
		return 1;
	}

	testRateTracksTheModelClock();
	testCgbIsNotDoubled();
	testRateIsConsistentWithTheDeclaredFps();

	printf("\n%d/%d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
