// Realtime BGM regression against real ROMs (the developer's own, never
// committed). Drives src/platform/libretro/realtime-bgm.c the way libretro.c
// does: main core frames, AfterMainFrame + GameAudio per main frame, Run +
// Read per 1x frame of wall time.
//
// usage: bgm-smoke rom.gba mode [warm frames speed]
//   find                  locate the driver, print the status and entries
//   exact  warm frames    main at 1x with the shadows active, PSG off: music lane vs main
//   split  warm frames    main at 1x: music lane + effects lane vs main, 0.5 s windows
//   ff     warm frames N  main at Nx for `frames` 1x frames; status must stay ok
//   merc   warm           Mercury: 6 s at 4x, release, re-press after 0.5 s, release again
// env: BIOS=path (main boots a real BIOS, as on device), PRESS (tap A+Start every 1.5 s; PRESS_UNTIL=n stops at main frame n), SHOT=path.wav (write the lane under test),
//      NOPSG (split: DirectSound only, as PSG phase alone drags waveform correlation)

#include "../../src/platform/libretro/aux-core.h"
#include "../../src/platform/libretro/realtime-bgm.h"
#include "../../src/platform/libretro/realtime-bgm-engine.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 65536
#define MERCURY_TEMPO 0x0203B798 // the game's own compensation result

static void quiet(struct mLogger* l, int c, enum mLogLevel v, const char* f, va_list a) {
	(void) l; (void) c; (void) v; (void) f; (void) a;
}
static struct mLogger logger = { .log = quiet };

static uint8_t* rom;
static size_t romSize;
static mColor video[256 * 224];
static struct mCore* mainCore;
static struct RealtimeBgm* bgm;
static int64_t wallMs = 1700000000000LL;
static long mainFrames;
static bool psgOff;

struct Pcm {
	int16_t* s;
	size_t n, cap;
};

static void append(struct Pcm* p, const int16_t* s, size_t n) {
	if (p->n + n > p->cap) {
		p->cap = (p->n + n) * 2;
		p->s = realloc(p->s, p->cap * 4);
	}
	memcpy(p->s + p->n * 2, s, n * 4);
	p->n += n;
}

static void drainInto(struct mAudioBuffer* b, struct Pcm* p) {
	int16_t tmp[4096 * 2];
	size_t got;
	while ((got = mAudioBufferRead(b, tmp, 4096)) > 0) {
		if (p) {
			append(p, tmp, got);
		}
	}
}

static void setPsg(struct mCore* core, bool on) {
	for (int c = 0; c < 4; ++c) {
		((struct GBA*) core->board)->audio.psg.forceDisableCh[c] = !on;
	}
}

// One main frame as retro_run does it. `mainPcm` gets main's own output (what
// a player hears without the shadows), `fx` the effects lane.
static void mainFrame(struct Pcm* mainPcm, struct Pcm* fx) {
	if (getenv("PRESS")) {
		long until = getenv("PRESS_UNTIL") ? atol(getenv("PRESS_UNTIL")) : -1;
		mainCore->setKeys(mainCore, (until < 0 || mainFrames < until) && mainFrames % 90 < 4 ? (1 | 1 << 3) : 0);
	}
	mainCore->runFrame(mainCore);
	++mainFrames;
	RealtimeBgmAfterMainFrame(bgm);
	struct mCore* fxCore = RealtimeBgmShadowForTesting(bgm, true);
	if (psgOff && fxCore) {
		setPsg(fxCore, false);
	}
	struct mAudioBuffer* own = mainCore->getAudioBuffer(mainCore);
	if (mainPcm) {
		int16_t tmp[4096 * 2];
		append(mainPcm, tmp, mAudioBufferDump(own, tmp, 4096, 0));
	}
	drainInto(RealtimeBgmGameAudio(bgm, own), fx);
	mAudioBufferClear(own);
}

// One 1x frame of wall time: the clock the game's RTC sees, and the music lane.
static void wallFrame(struct Pcm* music) {
	wallMs += 1000 / 60;
	mainCore->rtc.value = wallMs;
	struct mCore* shadow = RealtimeBgmShadowForTesting(bgm, false);
	if (psgOff && shadow) {
		setPsg(shadow, false);
	}
	RealtimeBgmRun(bgm, 1);
	int16_t tmp[4096 * 2];
	size_t got;
	while ((got = RealtimeBgmRead(bgm, tmp, 4096)) > 0) {
		if (music) {
			append(music, tmp, got);
		}
	}
}

static double corr(const int16_t* a, const int16_t* b, size_t n) {
	double ab = 0, aa = 0, bb = 0;
	for (size_t i = 0; i < n; ++i) {
		ab += (double) a[i] * b[i];
		aa += (double) a[i] * a[i];
		bb += (double) b[i] * b[i];
	}
	return aa && bb ? ab / sqrt(aa * bb) : 0;
}

static double rms(const int16_t* a, size_t n) {
	double s = 0;
	for (size_t i = 0; i < n; ++i) {
		s += (double) a[i] * a[i];
	}
	return n ? sqrt(s / n) : 0;
}

// Best correlation of `b` against `a` over lags of +-maxLag stereo frames.
static double bestCorr(const struct Pcm* a, const struct Pcm* b, size_t from, size_t frames, int maxLag) {
	double best = -2;
	for (int lag = -maxLag; lag <= maxLag; ++lag) {
		if ((long) from + lag < 0 || from + lag + frames > b->n || from + frames > a->n) {
			continue;
		}
		double c = corr(a->s + from * 2, b->s + (from + lag) * 2, frames * 2);
		if (c > best) {
			best = c;
		}
	}
	return best;
}

static void writeWav(const char* path, const struct Pcm* p) {
	FILE* f = fopen(path, "wb");
	uint32_t data = (uint32_t) p->n * 4, v;
	fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
	uint16_t h[2] = { 1, 2 }; fwrite(h, 2, 2, f);
	v = RATE; fwrite(&v, 4, 1, f); v = RATE * 4; fwrite(&v, 4, 1, f);
	uint16_t h2[2] = { 4, 16 }; fwrite(h2, 2, 2, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
	fwrite(p->s, 4, p->n, f);
	fclose(f);
}

static void makeMain(void) {
	mainCore = mCoreCreate(mPLATFORM_GBA);
	mCoreInitConfig(mainCore, NULL);
	mainCore->init(mainCore);
	RetroAuxCoreConfigure(mainCore);
	if (getenv("BIOS") && *getenv("BIOS")) {
		mCoreConfigSetIntValue(&mainCore->config, "useBios", 1);
		mCoreConfigSetIntValue(&mainCore->config, "skipBios", 0);
		mCoreLoadConfig(mainCore);
	}
	mainCore->loadROM(mainCore, VFileFromConstMemory(rom, romSize));
	if (getenv("BIOS") && *getenv("BIOS")) {
		mainCore->loadBIOS(mainCore, VFileOpen(getenv("BIOS"), O_RDONLY), 0);
	}
	mainCore->setVideoBuffer(mainCore, video, 256);
	mainCore->rtc.override = RTC_FIXED;
	mainCore->rtc.value = wallMs;
	mainCore->reset(mainCore);
	RetroAuxCoreNeverDraw(mainCore);
}

static void warmUp(int warm) {
	for (int f = 0; f < warm; ++f) {
		mainFrame(NULL, NULL);
		wallFrame(NULL);
	}
}

static int exactOrSplit(bool split, int warm, int frames) {
	psgOff = !split || getenv("NOPSG");
	if (psgOff) {
		setPsg(mainCore, false);
	}
	warmUp(warm);
	RealtimeBgmSetSpeed(bgm, 4, true);
	if (!RealtimeBgmActive(bgm)) {
		printf("no takeover (status %d)\n", RealtimeBgmStatus(bgm));
		return 1;
	}
	struct Pcm mainPcm = { 0 }, music = { 0 }, fx = { 0 };
	for (int f = 0; f < frames; ++f) {
		mainFrame(&mainPcm, &fx);
		wallFrame(&music);
	}
	if (!split) {
		size_t n = music.n < mainPcm.n ? music.n : mainPcm.n;
		double c = bestCorr(&mainPcm, &music, 4096, n - 8192, 2048);
		printf("exact corr=%.3f rms main=%.0f music=%.0f status %d\n", c, rms(mainPcm.s, n * 2), rms(music.s, n * 2),
		       RealtimeBgmStatus(bgm));
		if (getenv("SHOT")) {
			writeWav(getenv("SHOT"), &music);
		}
		return c >= 0.95 ? 0 : 1;
	}
	size_t n = music.n < fx.n ? music.n : fx.n;
	struct Pcm sum = { malloc(n * 4), n, n };
	for (size_t i = 0; i < n * 2; ++i) {
		int v = music.s[i] + fx.s[i];
		sum.s[i] = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
	const size_t window = RATE / 2;
	double total = 0;
	int windows = 0;
	for (size_t at = 4096; at + window + 4096 < n; at += window, ++windows) {
		double c = bestCorr(&mainPcm, &sum, at, window, 1300);
		printf("  t=%4.1fs rms main=%5.0f music=%5.0f fx=%5.0f corr=%.3f\n", at / (double) RATE, rms(mainPcm.s + at * 2, window * 2),
		       rms(music.s + at * 2, window * 2), rms(fx.s + at * 2, window * 2), c);
		total += c;
	}
	double mean = windows ? total / windows : 0;
	printf("split mean corr=%.3f over %d windows, status %d\n", mean, windows, RealtimeBgmStatus(bgm));
	if (getenv("SHOT")) {
		writeWav(getenv("SHOT"), &sum);
	}
	return mean >= 0.95 ? 0 : 1;
}

static int fastForward(int warm, int frames, int speed) {
	warmUp(warm);
	RealtimeBgmSetSpeed(bgm, speed, true);
	struct Pcm music = { 0 }, fx = { 0 };
	for (int f = 0; f < frames; ++f) {
		for (int k = 0; k < speed; ++k) {
			mainFrame(NULL, &fx);
		}
		wallFrame(&music);
	}
	printf("ff %dx: active=%d status=%d rms music=%.0f fx=%.0f\n", speed, RealtimeBgmActive(bgm), RealtimeBgmStatus(bgm),
	       rms(music.s, music.n * 2), rms(fx.s, fx.n * 2));
	if (getenv("SHOT")) {
		writeWav(getenv("SHOT"), &music);
	}
	return RealtimeBgmStatus(bgm) == RETRO_BGM_OK ? 0 : 1;
}

// Largest key shift on the effects shadow's playing tracks; 0 is the original pitch.
static int effectsKeyShift(void) {
	struct mCore* fxCore = RealtimeBgmShadowForTesting(bgm, true);
	struct ARMCore* cpu = ((struct GBA*) fxCore->board)->cpu;
	int worst = 0;
	uint32_t si = cpu->memory.load32(cpu, 0x03007FF0, NULL);
	uint32_t pi = cpu->memory.load32(cpu, si + 0x24, NULL);
	for (int n = 0; n < 64 && pi >= 0x02000000 && pi < 0x04000000; ++n, pi = cpu->memory.load32(cpu, pi + 0x3C, NULL)) {
		uint32_t st = cpu->memory.load32(cpu, pi + 4, NULL);
		if (!(st & 0xFFFF) || (st & 0x80000000)) {
			continue;
		}
		int key = (int8_t) cpu->memory.load8(cpu, cpu->memory.load32(cpu, pi + 0x2C, NULL) + 0x0B, NULL);
		if (abs(key) > abs(worst)) {
			worst = key;
		}
	}
	return worst;
}

static unsigned mercuryTempo(void) {
	struct ARMCore* cpu = ((struct GBA*) mainCore->board)->cpu;
	return cpu->memory.load16(cpu, MERCURY_TEMPO, NULL);
}

static void fast(int frames) {
	for (int f = 0; f < frames; ++f) {
		for (int k = 0; k < 4; ++k) {
			mainFrame(NULL, NULL);
		}
		wallFrame(NULL);
	}
}

// The game slows its own music for the speed it measures from the RTC; the
// hand-back must outlast that, and a re-press inside it must not reclone.
static int mercury(int warm) {
	warmUp(warm);
	RealtimeBgmSetSpeed(bgm, 4, true);
	int fails = 0, key = 0;
	for (int f = 0; f < 360; ++f) {
		fast(1);
		int k = effectsKeyShift();
		if (abs(k) > abs(key)) {
			key = k;
		}
	}
	printf("after 6 s at 4x: game tempo=%u, effects key shift=%d\n", mercuryTempo(), key);
	fails += mercuryTempo() == 256; // the game must have compensated, or nothing was tested
	fails += key != 0;
	unsigned clones = RealtimeBgmClonesForTesting();
	RealtimeBgmSetSpeed(bgm, 1, true);
	warmUp(30);
	bool heldBack = RealtimeBgmActive(bgm) && mercuryTempo() != 256;
	RealtimeBgmSetSpeed(bgm, 4, true);
	printf("re-press 0.5 s after release: still active=%d game tempo=%u reclones=%u\n", heldBack, mercuryTempo(),
	       RealtimeBgmClonesForTesting() - clones);
	fails += !heldBack;
	fails += RealtimeBgmClonesForTesting() != clones;
	fast(120);
	RealtimeBgmSetSpeed(bgm, 1, true);
	int f = 0;
	unsigned tempoAtHandBack = 0;
	for (; f < 600 && RealtimeBgmActive(bgm); ++f) {
		tempoAtHandBack = mercuryTempo();
		mainFrame(NULL, NULL);
		wallFrame(NULL);
	}
	printf("final release: handed back after %.2f s, game tempo then=%u\n", f / 60.0, tempoAtHandBack);
	fails += RealtimeBgmActive(bgm) || tempoAtHandBack != 256;
	return fails ? 1 : 0;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s rom.gba find|exact|split|ff|merc [warm frames speed]\n", argv[0]);
		return 2;
	}
	mLogSetDefaultLogger(&logger);
	struct VFile* vf = VFileOpen(argv[1], O_RDONLY);
	if (!vf) {
		return 2;
	}
	romSize = vf->size(vf);
	rom = malloc(romSize);
	vf->read(vf, rom, romSize);
	vf->close(vf);

	struct RealtimeBgmM4a m4a;
	enum retro_bgm_status status = RealtimeBgmLocate(rom, romSize, &m4a);
	printf("status %d SoundMain=%08X VSync=%08X MPlayStart=%08X MPlayStop=%08X\n", status, m4a.soundMain, m4a.soundVSync,
	       m4a.mplayStart, m4a.mplayStop);
	if (!strcmp(argv[2], "find") || status != RETRO_BGM_OK) {
		free(rom);
		return status == RETRO_BGM_OK ? 0 : 1;
	}
	makeMain();
	struct RealtimeBgmEngine engine;
	RealtimeBgmM4aEngine(mainCore, rom, romSize, &m4a, &engine);
	bgm = RealtimeBgmCreate(mainCore, rom, romSize, &engine, RATE);
	int warm = argc > 3 ? atoi(argv[3]) : 600;
	int frames = argc > 4 ? atoi(argv[4]) : 600;
	int rc = 2;
	if (!strcmp(argv[2], "exact") || !strcmp(argv[2], "split")) {
		rc = exactOrSplit(!strcmp(argv[2], "split"), warm, frames);
	} else if (!strcmp(argv[2], "ff")) {
		rc = fastForward(warm, frames, argc > 5 ? atoi(argv[5]) : 4);
	} else if (!strcmp(argv[2], "merc")) {
		rc = mercury(warm);
	}
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(mainCore);
	free(rom);
	return rc;
}
