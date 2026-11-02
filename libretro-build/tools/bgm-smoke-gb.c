// Realtime BGM regression for Pokémon GB/GBC against real ROMs (the
// developer's own, never committed). Drives realtime-bgm.c with the GB engine
// the way libretro.c does and compares the shadows' APU writes with a core
// that plays the same thing for real.
//
// usage: bgm-smoke-gb rom.gb[c] mode [warm frames speed]
//   find                    locate the engine, print the status and parameters
//   gbmusic warm frames N   main at Nx; music shadow vs a 1x reference, on frames with no effect
//   gbsfx   warm frames     lockstep; effects shadow vs main, per hardware channel an effect owns
//   ff      warm frames N   main at Nx; status must stay ok, both lanes audible
// env: PRESS (A / Start / Down held 6 frames every 40), GB_MODEL=DMG|SGB|CGB, BIOS=boot ROM,
//      DIFF=n (gbmusic: print the first n differing frames)

#include "../../src/platform/libretro/aux-core.h"
#include "../../src/platform/libretro/realtime-bgm.h"
#include "../../src/platform/libretro/realtime-bgm-engine.h"
#include "../../src/platform/libretro/realtime-bgm-gb.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NR50 0x24 // written by gen1's FadeOutAudio and mirrored by the module: not engine output

static void quiet(struct mLogger* l, int c, enum mLogLevel v, const char* f, va_list a) {
	(void) l; (void) c; (void) v; (void) f; (void) a;
}
static struct mLogger logger = { .log = quiet };

static uint8_t* rom;
static size_t romSize;
static mColor video[256 * 224];
static struct RealtimeBgmGb engineGb;
static struct mCore* mainCore;
static struct RealtimeBgm* bgm;

static struct mCore* makeCore(void) {
	struct mCore* c = mCoreCreate(mPLATFORM_GB);
	mCoreInitConfig(c, NULL);
	c->init(c);
	RetroAuxCoreConfigure(c);
	if (getenv("GB_MODEL")) {
		mCoreConfigSetValue(&c->config, "gb.model", getenv("GB_MODEL"));
		mCoreLoadConfig(c);
	}
	c->loadROM(c, VFileFromConstMemory(rom, romSize));
	if (getenv("BIOS") && *getenv("BIOS")) {
		c->loadBIOS(c, VFileOpen(getenv("BIOS"), O_RDONLY), 0);
		mCoreConfigSetValue(&c->config, "skipBios", "0");
		mCoreLoadConfig(c);
	}
	c->setVideoBuffer(c, video, 256);
	c->reset(c);
	RetroAuxCoreNeverDraw(c);
	return c;
}

static unsigned keys(long f) {
	if (!getenv("PRESS") || f % 40 >= 6) {
		return 0;
	}
	static const unsigned cycle[] = { 0x01, 0x08, 0x80 }; // A, Start, Down
	return cycle[(f / 40) % 3];
}

static void drain(struct mAudioBuffer* b, double* sq, size_t* n) {
	int16_t t[4096 * 2];
	size_t got;
	while ((got = mAudioBufferRead(b, t, 4096)) > 0) {
		for (size_t i = 0; sq && i < got * 2; ++i) {
			*sq += (double) t[i] * t[i];
		}
		if (n) {
			*n += got * 2;
		}
	}
}

static long frameNo;
static void mainFrame(double* fxSq, size_t* fxN) {
	mainCore->setKeys(mainCore, keys(frameNo++));
	mainCore->runFrame(mainCore);
	RealtimeBgmAfterMainFrame(bgm);
	drain(RealtimeBgmGameAudio(bgm, mainCore->getAudioBuffer(mainCore)), fxSq, fxN);
}

static void musicFrame(double* sq, size_t* n) {
	RealtimeBgmRun(bgm, 1);
	int16_t t[8192 * 2];
	size_t got;
	while ((got = RealtimeBgmRead(bgm, t, 8192)) > 0) {
		for (size_t i = 0; sq && i < got * 2; ++i) {
			*sq += (double) t[i] * t[i];
		}
		if (n) {
			*n += got * 2;
		}
	}
}

// APU writes per core, tagged with the frame: frame << 16 | reg << 8 | value.
struct Log {
	struct SM83Core* cpu;
	void (*orig)(struct SM83Core*, uint16_t, int8_t);
	uint32_t* w;
	size_t n, cap;
} logs[2];
static int curFrame;

static void logStore(struct SM83Core* cpu, uint16_t a, int8_t v) {
	struct Log* l = logs[0].cpu == cpu ? &logs[0] : &logs[1];
	if (a >= 0xFF10 && a < 0xFF40) {
		if (l->n == l->cap) {
			l->cap = l->cap ? l->cap * 2 : 4096;
			l->w = realloc(l->w, l->cap * 4);
		}
		l->w[l->n++] = (uint32_t) curFrame << 16 | (a & 0xFF) << 8 | (uint8_t) v;
	}
	l->orig(cpu, a, v);
}

static void hook(int i, struct mCore* c) {
	struct GB* gb = c->board;
	logs[i] = (struct Log) { .cpu = gb->cpu, .orig = gb->cpu->memory.store8 };
	gb->cpu->memory.store8 = logStore;
}

// FF10-14 ch1, 16-19 ch2, 1A-1E and wave RAM ch3, 20-23 ch4.
static int hwOf(uint8_t reg) {
	if (reg >= 0x10 && reg <= 0x14) return 0;
	if (reg >= 0x16 && reg <= 0x19) return 1;
	if ((reg >= 0x1A && reg <= 0x1E) || (reg >= 0x30 && reg <= 0x3F)) return 2;
	if (reg >= 0x20 && reg <= 0x23) return 3;
	return -1;
}

// The engine's own view of channel c (0-7): gen1's channel 8 drums are music.
static bool channelOn(struct mCore* c, int ch) {
	if (engineGb.family == REALTIME_BGM_GB_GEN1) {
		uint8_t id = c->busRead8(c, engineGb.soundIds + ch);
		return id && !(ch == 7 && id <= REALTIME_BGM_GB1_DRUM_MAX);
	}
	uint16_t base = engineGb.musicPlaying + 1 + ch * engineGb.channelLength;
	return c->busRead8(c, base + engineGb.channelFlags) & 1;
}

static bool anyEffect(struct mCore* c) {
	for (int ch = 4; ch < 8; ++ch) {
		if (channelOn(c, ch)) {
			return true;
		}
	}
	return false;
}

// Writes of one frame (optionally one hardware channel), NR50 aside.
static size_t frameWrites(const struct Log* l, int f, int hw, uint16_t* out, size_t max) {
	size_t n = 0;
	for (size_t i = 0; i < l->n && n < max; ++i) {
		int wf = l->w[i] >> 16;
		uint8_t reg = l->w[i] >> 8 & 0xFF;
		if (wf == f && reg != NR50 && (hw < 0 || hwOf(reg) == hw)) {
			out[n++] = l->w[i] & 0xFFFF;
		}
	}
	return n;
}

static bool sameWrites(int fa, int fb, int hw) {
	uint16_t a[256], b[256];
	size_t na = frameWrites(&logs[0], fa, hw, a, 256), nb = frameWrites(&logs[1], fb, hw, b, 256);
	return na == nb && !memcmp(a, b, na * 2);
}

static void printWrites(const char* who, int f) {
	uint16_t w[256];
	size_t n = frameWrites(&logs[who[0] == 's'], f, -1, w, 256);
	printf("  f%d %s:", f, who);
	for (size_t i = 0; i < n; ++i) {
		printf(" %02X=%02X", w[i] >> 8, w[i] & 0xFF);
	}
	printf("\n");
}

static void warmUp(int warm) {
	for (int f = 0; f < warm; ++f) {
		mainFrame(NULL, NULL);
	}
}

static int gbmusic(int warm, int frames, int speed) {
	warmUp(warm);
	struct mCore* ref = makeCore();
	size_t size = mainCore->stateSize(mainCore);
	void* state = malloc(size);
	mainCore->saveState(mainCore, state);
	ref->loadState(ref, state);
	free(state);
	RealtimeBgmSetSpeed(bgm, speed > 1 ? speed : 2, true);
	hook(0, ref);
	hook(1, RealtimeBgmShadowForTesting(bgm, false));
	bool* fx = calloc(frames, sizeof(bool));
	for (int f = 0; f < frames; ++f) {
		for (int k = 0; k < speed; ++k) {
			mainFrame(NULL, NULL);
		}
		curFrame = f;
		ref->runFrame(ref);
		drain(ref->getAudioBuffer(ref), NULL, NULL);
		fx[f] = anyEffect(ref);
		musicFrame(NULL, NULL);
	}
	int checked = 0, same = 0;
	for (int f = 5; f < frames - 5; ++f) {
		if (fx[f] || fx[f - 1]) {
			continue;
		}
		++checked;
		bool ok = sameWrites(f, f, -1);
		same += ok;
		static int shown;
		if (!ok && getenv("DIFF") && shown++ < atoi(getenv("DIFF"))) {
			printWrites("ref", f);
			printWrites("shadow", f);
		}
	}
	printf("music %dx: identical %d/%d (%.1f%%) status=%d\n", speed, same, checked, checked ? 100.0 * same / checked : 0,
	       RealtimeBgmStatus(bgm));
	free(fx);
	RetroAuxCoreDestroy(ref);
	return RealtimeBgmStatus(bgm) == RETRO_BGM_OK ? 0 : 1;
}

static int gbsfx(int warm, int frames) {
	warmUp(warm);
	RealtimeBgmSetSpeed(bgm, 2, true);
	hook(0, mainCore);
	hook(1, RealtimeBgmShadowForTesting(bgm, true));
	int owned = 0, same = 0;
	for (int f = 0; f < frames; ++f) {
		curFrame = f;
		mainFrame(NULL, NULL);
		for (int hw = 0; hw < 4; ++hw) {
			if (channelOn(mainCore, hw + 4)) {
				++owned;
				same += sameWrites(f, f, hw);
			}
		}
	}
	printf("effects: identical %d/%d (%.1f%%) status=%d\n", same, owned, owned ? 100.0 * same / owned : 0,
	       RealtimeBgmStatus(bgm));
	return RealtimeBgmStatus(bgm) == RETRO_BGM_OK ? 0 : 1;
}

static int fastForward(int warm, int frames, int speed) {
	warmUp(warm);
	RealtimeBgmSetSpeed(bgm, speed, true);
	double musicSq = 0, fxSq = 0;
	size_t musicN = 0, fxN = 0;
	for (int f = 0; f < frames; ++f) {
		for (int k = 0; k < speed; ++k) {
			mainFrame(&fxSq, &fxN);
		}
		musicFrame(&musicSq, &musicN);
	}
	printf("ff %dx: active=%d status=%d rms music=%.0f fx=%.0f\n", speed, RealtimeBgmActive(bgm), RealtimeBgmStatus(bgm),
	       musicN ? sqrt(musicSq / musicN) : 0, fxN ? sqrt(fxSq / fxN) : 0);
	return RealtimeBgmStatus(bgm) == RETRO_BGM_OK ? 0 : 1;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s rom.gb find|gbmusic|gbsfx|ff [warm frames speed]\n", argv[0]);
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

	enum retro_bgm_status status = RealtimeBgmGbLocate(rom, romSize, &engineGb);
	int banks = 0;
	for (int b = 0; b < 256; ++b) {
		banks += engineGb.updateMusic[b] != 0;
	}
	printf("status %d family %d gen2 %02X:%04X wMusicPlaying=%04X length=%02X flags=%d gen1 ids=%04X banks=%d\n", status,
	       engineGb.family, engineGb.updateBank, engineGb.updateSound, engineGb.musicPlaying, engineGb.channelLength,
	       engineGb.channelFlags, engineGb.soundIds, banks);
	if (!strcmp(argv[2], "find") || status != RETRO_BGM_OK) {
		free(rom);
		return status == RETRO_BGM_OK ? 0 : 1;
	}
	mainCore = makeCore();
	struct RealtimeBgmEngine engine;
	RealtimeBgmGbEngine(mainCore, &engineGb, &engine);
	bgm = RealtimeBgmCreate(mainCore, rom, romSize, &engine, mainCore->audioSampleRate(mainCore));
	int warm = argc > 3 ? atoi(argv[3]) : 600;
	int frames = argc > 4 ? atoi(argv[4]) : 600;
	int speed = argc > 5 ? atoi(argv[5]) : 4;
	int rc = 2;
	if (!strcmp(argv[2], "gbmusic")) {
		rc = gbmusic(warm, frames, speed);
	} else if (!strcmp(argv[2], "gbsfx")) {
		rc = gbsfx(warm, frames);
	} else if (!strcmp(argv[2], "ff")) {
		rc = fastForward(warm, frames, speed);
	}
	RealtimeBgmDestroy(bgm);
	RetroAuxCoreDestroy(mainCore);
	free(rom);
	return rc;
}
