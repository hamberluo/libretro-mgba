// Spike: shadow BGM. A second GBA instance cloned from the main one, CPU
// parked in halt, runs only the game's own m4a SoundVSync/SoundMain at 1x
// while the main instance runs at Nx. BGM player state is synced
// declaratively (song header / stop) by calling the game's MPlayStart.
//
// usage: spike rom.gba mode [args]
//   find                      locate m4a entry points
//   exact  warm K             clone at frame `warm`; compare shadow vs real 1x for K frames
//   ff     warm K N           main at Nx for K shadow frames; writes main.wav / shadow.wav

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void quiet(struct mLogger* l, int c, enum mLogLevel v, const char* f, va_list a) {
	(void) l; (void) c; (void) v; (void) f; (void) a;
}
static struct mLogger logger = { .log = quiet };

static uint8_t* rom;
static size_t romSize;
static mColor video[256 * 224];

struct M4a {
	uint32_t soundMain, soundVSync, songNumStart, mplayStart, mplayStop, mplayTable, songTable;
};

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }

// Thumb `ldr rX, [pc, #imm]` at `off` -> literal value.
static uint32_t thumbLiteral(size_t off) {
	uint16_t op = rd16(rom + off);
	size_t lit = ((off + 4) & ~3u) + (op & 0xFF) * 4;
	return rd32(rom + lit);
}

static uint32_t thumbBlTarget(size_t off) {
	uint16_t hi = rd16(rom + off), lo = rd16(rom + off + 2);
	int32_t o = ((int32_t) ((hi & 0x7FF) << 21) >> 9) | ((lo & 0x7FF) << 1);
	return (uint32_t) (0x08000000 + off + 4 + o);
}

static bool match(size_t off, const int16_t* pat, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		if (pat[i] >= 0 && rom[off + i] != pat[i]) return false;
	}
	return true;
}

// Thumb `ldr rX, [pc, #imm]` -> literal, or -1 if not that instruction.
static int64_t litAt(size_t off) {
	uint16_t op = rd16(rom + off);
	if ((op >> 11) != 0x09) return -1;
	size_t lit = ((off + 4) & ~3u) + (op & 0xFF) * 4;
	return lit + 4 <= romSize ? rd32(rom + lit) : -1;
}

// Nearest preceding `push {..., lr}` within `range` bytes.
static uint32_t funcStart(size_t off, size_t range) {
	for (size_t b = 0; b <= range && b <= off; b += 2) {
		if ((rd16(rom + off - b) & 0xFF00) == 0xB500) return 0x08000000 + off - b;
	}
	return 0;
}

// VSync: loads SOUND_INFO_PTR, checks ident (cmp ID or add -ID), then
// decrements the pcmDmaCounter byte at +4.
static uint32_t findVSyncSemantic(void) {
	for (size_t off = 0; off + 64 < romSize; off += 2) {
		if (litAt(off) != 0x03007FF0) continue;
		bool ident = false;
		for (size_t d = 2; d < 16; d += 2) {
			int64_t l = litAt(off + d);
			if (l == 0x68736D53 || l == (uint32_t) -0x68736D53) ident = true;
		}
		if (!ident) continue;
		for (size_t d = 2; d < 40; d += 2) {
			uint16_t a = rd16(rom + off + d);
			if ((a & 0xFFC0) != 0x7900) continue; // ldrb rd,[rn,#4]
			int rd = a & 7, rn = (a >> 3) & 7;
			uint16_t b = rd16(rom + off + d + 2);
			if (b != (0x3800 | rd << 8 | 1) && b != (0x1E40 | rd << 3 | rd)) continue; // subs rd,#1
			for (size_t e = 4; e < 10; e += 2) {
				uint16_t c = rd16(rom + off + d + e);
				if ((c & 0xFFC0) == 0x7100 && ((c >> 3) & 7) == rn) { // strb _,[rn,#4]
					uint32_t f = funcStart(off, 8);
					return f ? f : 0x08000000 + off;
				}
			}
		}
	}
	return 0;
}

// Number of Thumb BLs in the ROM that land on `target`.
static unsigned blCallers(size_t target) {
	unsigned n = 0;
	for (size_t off = 0; off + 4 <= romSize; off += 2) {
		uint16_t hi = rd16(rom + off);
		if ((hi & 0xF800) != 0xF000) continue;
		uint16_t lo = rd16(rom + off + 2);
		if ((lo & 0xF800) != 0xF800) continue;
		int32_t o = ((int32_t) ((hi & 0x7FF) << 21) >> 9) | ((lo & 0x7FF) << 1);
		n += (int64_t) off + 4 + o == (int64_t) target;
	}
	return n;
}

static bool isTerminator(uint16_t op) {
	return (op & 0xFF87) == 0x4700 /* bx */ || (op & 0xFF00) == 0xBD00 /* pop {..,pc} */ || op == 0x46C0 /* nop pad */;
}

static uint32_t funcEntryPush(size_t off, size_t range);
// Function entry for code at `off`: its prologue push, or an instruction just
// above it when gcc scheduled loads ahead of the push. Candidates stop at the
// previous function's end; the one most BLs land on wins, so a data word that
// happens to decode as BL cannot pull the entry into the middle of a function.
static uint32_t funcEntry(size_t off, size_t range) {
	uint32_t push = funcEntryPush(off, range);
	if (!push) return 0;
	size_t p = push - 0x08000000, best = p;
	unsigned bestN = blCallers(p);
	for (size_t c = p; c >= 2 && p - c < 16; c -= 2) {
		if (isTerminator(rd16(rom + c - 2))) break;
		unsigned n = blCallers(c - 2);
		if (n > bestN) { best = c - 2; bestN = n; }
	}
	return 0x08000000 + best;
}

// Function entry: nearest preceding `push {..., lr}`, extended over a gcc
// prologue (`push {..,lr}; mov rX, r8-r11...; push {..,lr}`).
static uint32_t funcEntryPush(size_t off, size_t range) {
	uint32_t f = funcStart(off, range);
	if (!f) return 0;
	size_t p = f - 0x08000000;
	for (;;) {
		size_t q = p;
		while (q >= 2 && (rd16(rom + q - 2) & 0xFF40) == 0x4640) q -= 2; // mov rX, r8-r15 (rX low or high)
		if (q == p || q < 2 || (rd16(rom + q - 2) & 0xFF00) != 0xB500) break;
		p = q - 2;
	}
	return 0x08000000 + p;
}

// MPlayStart: the only function that resets tempoD to 150 (+0x1C), as a
// halfword (agbcc) or merged word 0x01000096 (gcc), in a body that also writes
// the player's ident lock at +0x34.
static uint32_t findMPlayStartSemantic(void) {
	for (size_t off = 0; off + 64 < romSize; off += 2) {
		uint16_t t = rd16(rom + off);
		bool tempo = false;
		if ((t & 0xF8FF) == 0x2096) {
			int rt = (t >> 8) & 7;
			for (size_t e = 2; e < 16; e += 2) if ((rd16(rom + off + e) & 0xFFC7) == (0x8380 | rt)) tempo = true;
		} else if (litAt(off) == 0x01000096) {
			int rt = (t >> 8) & 7;
			for (size_t e = 2; e < 16; e += 2) if ((rd16(rom + off + e) & 0xFFC7) == (0x6000 | 7 << 6 | rt)) tempo = true; // str rt,[_,#0x1C]
		}
		if (!tempo) continue;
		uint32_t f = funcEntry(off, 0xC0);
		if (!f) continue;
		bool lock = false;
		for (size_t o = f - 0x08000000; o < off + 0x40; o += 2) {
			if ((rd16(rom + o) & 0xFFC0) == (0x6000 | 13 << 6)) lock = true; // str _,[_,#0x34]
		}
		if (lock) return f;
	}
	return 0;
}

// MPlayStop: sets the pause bit (0x80 << 24) in status (+4), then walks the
// tracks (stride 0x50) calling TrackStop. Several copies may exist (Kirby);
// the real one follows MPlayStart in the same compilation unit.
static uint32_t findMPlayStopSemantic(uint32_t after) {
	size_t from = after ? after - 0x08000000 : 0;
	for (size_t off = from; off + 64 < romSize && off < from + 0x800; off += 2) {
		uint16_t a = rd16(rom + off);
		if ((a & 0xF8FF) != 0x2080) continue; // movs rx,#0x80
		int rx = (a >> 8) & 7;
		bool shifted = false;
		for (size_t d = 2; d <= 6; d += 2) if (rd16(rom + off + d) == (0x0600 | 24 << 6 | rx << 3 | rx)) shifted = true;
		if (!shifted) continue;
		bool status = false, loop = false, call = false;
		for (size_t d = 4; d < 20; d += 2) if ((rd16(rom + off + d) & 0xFFC0) == 0x6040) status = true; // str _,[_,#4]
		for (size_t d = 4; d < 48; d += 2) {
			uint16_t c = rd16(rom + off + d);
			if ((c & 0xF8FF) == 0x3050) loop = true; // adds ry,#0x50
			if ((c & 0xF800) == 0xF000) call = true; // bl
		}
		if (status && loop && call) {
			uint32_t f = funcEntry(off, 0x40);
			if (f && f > after) return f;
		}
	}
	return 0;
}

static bool findM4a(struct M4a* m) {
	memset(m, 0, sizeof(*m));
	// SoundMain: ldr r0,=SOUND_INFO_PTR; ldr r0,[r0]; ldr r2,=ID; ldr r3,[r0]; cmp r2,r3; beq; bx lr; adds r3,1; str r3,[r0]
	static const int16_t sm[] = { -1, 0x48, 0x00, 0x68, -1, 0x4A, 0x03, 0x68, 0x9A, 0x42, -1, 0xD0, 0x70, 0x47, 0x01, 0x33, 0x03, 0x60 };
	// m4aSoundVSync: same head, then subs r3,r2; cmp r3,1
	static const int16_t vs[] = { -1, 0x48, 0x00, 0x68, -1, 0x4A, 0x03, 0x68, 0x9B, 0x1A, 0x01, 0x2B };
	// m4aSongNumStart (Sappy locator signature)
	static const int16_t sn[] = { 0x00, 0xB5, 0x00, 0x04, 0x07, 0x4A, 0x08, 0x49, 0x40, 0x0B, 0x40, 0x18, 0x83, 0x88, 0x59, 0x00, 0xC9, 0x18, 0x89, 0x00, 0x89, 0x18, 0x0A, 0x68, 0x01, 0x68, 0x10, 0x1C, 0x00, 0xF0 };
	// MPlayStart: push {r4-r7,lr}; mov r7,r8; push {r7}; mov r5,r0; mov r7,r1; ldr r1,[r5,#ident]; ldr r0,=ID; cmp r1,r0
	static const int16_t ms[] = { 0xF0, 0xB5, 0x47, 0x46, 0x80, 0xB4, 0x05, 0x1C, 0x0F, 0x1C, 0x69, 0x6B, -1, 0x48, 0x81, 0x42 };
	for (size_t off = 0; off + 64 < romSize; off += 2) {
		if (!m->soundMain && match(off, sm, sizeof(sm) / 2) && thumbLiteral(off) == 0x03007FF0 && thumbLiteral(off + 4) == 0x68736D53) {
			m->soundMain = 0x08000000 + off;
		}
		if (!m->soundVSync && match(off, vs, sizeof(vs) / 2) && thumbLiteral(off) == 0x03007FF0 && thumbLiteral(off + 4) == 0x68736D53) {
			m->soundVSync = 0x08000000 + off;
		}
		if (!m->mplayStart && match(off, ms, sizeof(ms) / 2) && thumbLiteral(off + 12) == 0x68736D53) {
			m->mplayStart = 0x08000000 + off;
		}
		if (!m->songNumStart && match(off, sn, sizeof(sn) / 2)) {
			m->songNumStart = 0x08000000 + off;
			m->mplayTable = thumbLiteral(off + 4);
			m->songTable = thumbLiteral(off + 6);
		}
	}
	if (getenv("SEMANTIC")) {
		m->soundVSync = m->mplayStart = 0;
	}
	if (!m->soundVSync) m->soundVSync = findVSyncSemantic();
	if (!m->mplayStart) m->mplayStart = findMPlayStartSemantic();
	m->mplayStop = findMPlayStopSemantic(m->mplayStart);
	return m->soundMain && m->soundVSync && m->mplayStart && m->mplayStop;
}

#define MAX_PLAYERS 64
static int playerCount;

static bool inRom(uint32_t a) { return a >= 0x08000000 && a - 0x08000000 < romSize; }

// A song is music if any track jumps backwards (GOTO) before its first FINE.
static bool trackLoops(uint32_t trk) {
	if (!inRom(trk)) return false;
	size_t start = trk - 0x08000000, end = start + 0x10000 < romSize ? start + 0x10000 : romSize - 6;
	// Light parse up to the first FINE: pointer arguments are skipped so
	// their bytes are never read as commands.
	for (size_t i = start; i + 6 < end; ++i) {
		uint8_t c = rom[i];
		if (c == 0xB1) return false;
		if (c == 0xB2) {
			uint32_t to = rd32(rom + i + 1);
			if (to >= trk && to < 0x08000000 + i) return true;
			i += 4;
		} else if (c == 0xB3) {
			i += 4;
		} else if (c == 0xB5) {
			i += 5;
		}
	}
	return false;
}

static bool songLoops(uint32_t header) {
	if (!inRom(header) || !inRom(header + 8)) return false;
	unsigned n = rom[header - 0x08000000];
	if (n > 16) n = 16;
	for (unsigned t = 0; t < n; ++t) {
		if (!inRom(header + 8 + t * 4 + 4)) break;
		if (trackLoops(rd32(rom + header - 0x08000000 + 8 + t * 4))) return true;
	}
	return false;
}

static uint32_t loopCacheKey[1024];
static int8_t loopCacheVal[1024];
static bool songLoopsCached(uint32_t h) {
	unsigned i = (h >> 2) & 1023;
	while (loopCacheKey[i] && loopCacheKey[i] != h) i = (i + 1) & 1023;
	if (!loopCacheKey[i]) { loopCacheKey[i] = h; loopCacheVal[i] = songLoops(h); }
	return loopCacheVal[i];
}


// Music players learned by watching main all session (1x included): a player
// that has played a looping song is a music player from then on.
static uint32_t learned[MAX_PLAYERS];
static int learnedCount;
static bool isLearned(uint32_t info) {
	for (int i = 0; i < learnedCount; ++i) if (learned[i] == info) return true;
	return false;
}
static uint32_t bus32(struct GBA* gba, uint32_t a);
static bool songLoopsCached(uint32_t h);
static void observe(struct GBA* g) {
	uint32_t si = bus32(g, 0x03007FF0);
	if (bus32(g, si) - 0x68736D53 > 1) return;
	int n = 0;
	for (uint32_t pi = bus32(g, si + 0x24); pi >= 0x02000000 && pi < 0x04000000 && n < MAX_PLAYERS; pi = bus32(g, pi + 0x3C), ++n) {
		if (bus32(g, pi + 0x34) - 0x68736D53 > 1) break;
		uint32_t st = bus32(g, pi + 4);
		if ((st & 0xFFFF) && !(st & 0x80000000) && !isLearned(pi) && learnedCount < MAX_PLAYERS && songLoopsCached(bus32(g, pi))) learned[learnedCount++] = pi;
	}
}

static struct mCore* makeCore(void) {
	struct mCore* core = mCoreCreate(mPLATFORM_GBA);
	mCoreInitConfig(core, NULL);
	core->init(core);
	struct mCoreOptions opts = { .skipBios = true, .useBios = false, .sampleRate = 65536, .volume = 0x100 };
	mCoreConfigLoadDefaults(&core->config, &opts);
	core->loadConfig(core, &core->config);
	core->setVideoBuffer(core, video, 256);
	core->loadROM(core, VFileFromConstMemory(rom, romSize));
	core->opts.volume = GBA_AUDIO_VOLUME_MAX;
	core->reset(core);
	((struct GBA*) core->board)->audio.masterVolume = GBA_AUDIO_VOLUME_MAX;
	if (getenv("NOPSG")) for (int c = 0; c < 4; ++c) ((struct GBA*) core->board)->audio.psg.forceDisableCh[c] = true;
	if (getenv("NODS")) { ((struct GBA*) core->board)->audio.forceDisableChA = true; ((struct GBA*) core->board)->audio.forceDisableChB = true; }
	return core;
}

static void clone(struct mCore* from, struct mCore* to) {
	size_t n = from->stateSize(from);
	void* buf = malloc(n);
	from->saveState(from, buf);
	to->loadState(to, buf);
	free(buf);
}

// --- WAV / audio capture ----------------------------------------------------

struct Pcm { int16_t* s; size_t n, cap; };

static void drain(struct mCore* core, struct Pcm* out) {
	struct mAudioBuffer* b = core->getAudioBuffer(core);
	int16_t tmp[4096 * 2];
	size_t got;
	while ((got = mAudioBufferRead(b, tmp, 4096)) > 0) {
		if (!out) continue;
		if (out->n + got > out->cap) {
			out->cap = (out->n + got) * 2;
			out->s = realloc(out->s, out->cap * 4);
		}
		memcpy(out->s + out->n * 2, tmp, got * 4);
		out->n += got;
	}
}

static void writeWav(const char* path, const struct Pcm* p, unsigned rate) {
	FILE* f = fopen(path, "wb");
	uint32_t data = (uint32_t) p->n * 4, v;
	fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
	uint16_t h[2] = { 1, 2 }; fwrite(h, 2, 2, f);
	fwrite(&rate, 4, 1, f); v = rate * 4; fwrite(&v, 4, 1, f);
	uint16_t h2[2] = { 4, 16 }; fwrite(h2, 2, 2, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
	fwrite(p->s, 4, p->n, f);
	fclose(f);
}

// --- shadow -------------------------------------------------------------------

#define RET_ADDR 0x08000000u
static unsigned long budgetHits;

// Calls a Thumb function in the shadow; returns false if it blew the budget.
static bool shadowCall(struct GBA* gba, uint32_t fn, uint32_t r0, uint32_t r1, uint32_t sp) {
	struct ARMCore* cpu = gba->cpu;
	cpu->halted = 0;
	cpu->cpsr.i = 1;
	ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
	cpu->gprs[0] = r0;
	cpu->gprs[1] = r1;
	cpu->gprs[ARM_SP] = sp;
	cpu->gprs[ARM_LR] = RET_ADDR | 1;
	cpu->cpsr.t = 1;
	_ARMSetMode(cpu, MODE_THUMB);
	cpu->gprs[ARM_PC] = fn & ~1u;
	ThumbWritePC(cpu);
	long budget = 2000000;
	while (budget-- > 0) {
		uint32_t pc = cpu->gprs[ARM_PC];
		if (pc >= RET_ADDR && pc <= RET_ADDR + 8) {
			GBAHalt(gba);
			return true;
		}
		ARMRun(cpu);
	}
	++budgetHits;
	GBAHalt(gba);
	return false;
}

struct Shadow {
	struct mCore* core;
	struct GBA* gba;
	uint32_t sp;
	uint32_t bgmInfo; // first music player, for logging
	uint32_t info[MAX_PLAYERS];
	bool sfx; // plays sound effects in lockstep with main instead of music at 1x
	unsigned starts, stops, continues, fades, controls, ramHeaders;
};

static uint32_t bus32(struct GBA* gba, uint32_t a) { return gba->cpu->memory.load32(gba->cpu, a, NULL); }

static void shadowInit(struct Shadow* s, struct mCore* main, const struct M4a* m) {
	s->core = makeCore();
	clone(main, s->core);
	s->gba = s->core->board;
	// A shadow only ever needs sound: never render (as link.c's _neverDraw).
	s->gba->video.frameskip = INT_MAX;
	s->gba->video.frameskipCounter = INT_MAX;
	// Below the system-mode SP is free stack by the ABI; game code never runs here again.
	ARMSetPrivilegeMode(s->gba->cpu, MODE_SYSTEM);
	s->sp = s->gba->cpu->gprs[ARM_SP];
	s->bgmInfo = 0;
	// Players from the driver's own chain: SoundInfo.musicPlayerHead, then info->intp.
	uint32_t si = bus32(s->gba, 0x03007FF0);
	playerCount = 0;
	for (uint32_t pi = bus32(s->gba, si + 0x24); pi >= 0x02000000 && pi < 0x04000000 && playerCount < MAX_PLAYERS; pi = bus32(s->gba, pi + 0x3C)) {
		if (bus32(s->gba, pi + 0x34) != 0x68736D53) break;
		bool seen = false;
		for (int q = 0; q < playerCount; ++q) seen |= s->info[q] == pi;
		if (seen) { printf("chain: cycle at %08X\n", pi); break; }
		s->info[playerCount++] = pi;
	}
	printf("chain: %d players\n", playerCount);
	for (int p = 0; p < playerCount; ++p) {
		if (!s->info[p]) continue;
		// Untouched: what is sounding at the clone keeps sounding (a sound
		// effect ends on its own; a one-shot intro or fanfare plays on).
		if (isLearned(s->info[p]) && !s->bgmInfo) s->bgmInfo = s->info[p];
	}
	if (!s->bgmInfo && playerCount) s->bgmInfo = s->info[0];
	// Nothing but our calls may wake the CPU.
	s->gba->memory.io[GBA_REG(IE)] = 0;
	s->gba->memory.io[GBA_REG(IME)] = 0;
	GBAHalt(s->gba);
	drain(s->core, NULL);
}

// One 1x frame: run to VBlank, then the driver's per-frame calls.
static void shadowFrame(struct Shadow* s, const struct M4a* m) {
	s->core->runFrame(s->core);
	shadowCall(s->gba, m->soundVSync, 0, 0, s->sp);
	shadowCall(s->gba, m->soundMain, 0, 0, s->sp);
}

// Declarative sync of the BGM player from main.
static void shadowSyncPlayer(struct Shadow* s, struct GBA* main, const struct M4a* m, uint32_t info);
static void shadowSync(struct Shadow* s, struct GBA* main, const struct M4a* m) {
	for (int p = 0; p < playerCount; ++p) {
		if (s->info[p]) shadowSyncPlayer(s, main, m, s->info[p]);
	}
}

static bool inRam(uint32_t a) { return (a >= 0x02000000 && a < 0x02040000) || (a >= 0x03000000 && a < 0x03008000); }

static void copyBytes(struct Shadow* s, struct GBA* main, uint32_t a, uint32_t n) {
	for (uint32_t i = 0; i < n; ++i) {
		s->gba->cpu->memory.store8(s->gba->cpu, a + i, main->cpu->memory.load8(main->cpu, a + i, NULL), NULL);
	}
}

// A header the game built in RAM (Pokemon cries carry per-species pitch and
// length there): the shadow's copy is from the clone, so bring it up to date.
static void copyRamSong(struct Shadow* s, struct GBA* main, uint32_t header) {
	if (!inRam(header)) return;
	unsigned n = main->cpu->memory.load8(main->cpu, header, NULL);
	if (n > 16) n = 16;
	copyBytes(s, main, header, 8 + 4 * n);
	for (unsigned t = 0; t < n; ++t) {
		uint32_t trk = bus32(main, header + 8 + 4 * t);
		if (inRam(trk)) copyBytes(s, main, trk, 0x100);
	}
	uint32_t tone = bus32(main, header + 4);
	if (inRam(tone)) copyBytes(s, main, tone, 12);
	++s->ramHeaders;
}

// Per-track volume/pan/pitch controls the game applied after starting the
// song: exactly m4aMPlayVolumeControl / PanpotControl / PitchControl.
static bool compensating(struct Shadow* s, struct GBA* main) {
	for (int p = 0; p < playerCount; ++p) {
		uint32_t pi = s->info[p]; if (!pi || !isLearned(pi)) continue;
		uint32_t st = bus32(main, pi + 4);
		if ((st & 0xFFFF) && !(st & 0x80000000) && main->cpu->memory.load16(main->cpu, pi + 0x1E, NULL) != 0x100) return true;
	}
	return false;
}
static unsigned pitchSkipped;
static void copyControls(struct Shadow* s, struct GBA* main, uint32_t info) {
	(void) compensating; bool skipPitch = getenv("RULE") && main->cpu->memory.load16(main->cpu, info + 0x1E, NULL) != 0x100;
	if (skipPitch) ++pitchSkipped;
	struct ARMCore* mc = main->cpu, *sc = s->gba->cpu;
	uint32_t tracks = bus32(main, info + 0x2C);
	unsigned n = mc->memory.load8(mc, info + 8, NULL);
	if (n > 16) n = 16;
	for (unsigned t = 0; t < n; ++t) {
		uint32_t tr = tracks + t * 0x50;
		uint8_t flags = 0;
		static const struct { uint8_t off, flag; } f[] = { { 0x13, 0x03 }, { 0x15, 0x03 }, { 0x0B, 0x0C }, { 0x0D, 0x0C } };
		for (unsigned i = 0; i < (skipPitch ? 2u : 4u); ++i) {
			uint8_t v = mc->memory.load8(mc, tr + f[i].off, NULL);
			if (v != sc->memory.load8(sc, tr + f[i].off, NULL)) { sc->memory.store8(sc, tr + f[i].off, v, NULL); flags |= f[i].flag; }
		}
		if (flags) { sc->memory.store8(sc, tr, sc->memory.load8(sc, tr, NULL) | flags, NULL); ++s->controls; }
	}
}

static void shadowSyncPlayer(struct Shadow* s, struct GBA* main, const struct M4a* m, uint32_t info) {
	uint32_t mHeader = bus32(main, info), sHeader = bus32(s->gba, info);
	uint32_t mStatus = bus32(main, info + 4), sStatus = bus32(s->gba, info + 4);
	bool mPlaying = (mStatus & 0xFFFF) && !(mStatus & 0x80000000);
	bool sPlaying = (sStatus & 0xFFFF) && !(sStatus & 0x80000000);
	struct ARMCore* sc = s->gba->cpu;
	bool music = isLearned(info) || songLoopsCached(mHeader);
	bool wanted = s->sfx ? !music : music;
	if (mPlaying && mHeader != sHeader) {
		if (wanted) {
			copyRamSong(s, main, mHeader);
			shadowCall(s->gba, m->mplayStart, info, mHeader, s->sp);
			++s->starts;
		} else if (sPlaying) {
			shadowCall(s->gba, m->mplayStop, info, 0, s->sp);
			++s->stops;
		}
	} else if (mPlaying && !sPlaying && (sStatus & 0xFFFF) && wanted) {
		// Same song resumed: exactly m4aMPlayContinue (clear the pause bit).
		sc->memory.store32(sc, info + 4, sStatus & 0x7FFFFFFF, NULL);
		++s->continues;
	} else if (mPlaying && !sPlaying && wanted) {
		copyRamSong(s, main, mHeader);
		shadowCall(s->gba, m->mplayStart, info, mHeader, s->sp);
		++s->starts;
	} else if (sPlaying && (!mPlaying || !wanted)) {
		shadowCall(s->gba, m->mplayStop, info, 0, s->sp);
		++s->stops;
	}
	// A fade the game began: exactly m4aMPlayFadeOut/In (fadeOI/OC/OV).
	uint16_t mFade = main->cpu->memory.load16(main->cpu, info + 0x24, NULL);
	if (mFade && !sc->memory.load16(sc, info + 0x24, NULL)) {
		sc->memory.store16(sc, info + 0x24, mFade, NULL);
		sc->memory.store16(sc, info + 0x26, main->cpu->memory.load16(main->cpu, info + 0x26, NULL), NULL);
		sc->memory.store16(sc, info + 0x28, main->cpu->memory.load16(main->cpu, info + 0x28, NULL), NULL);
		++s->fades;
	}
	// Music ignores volume/tempo/pitch control on purpose (cry ducking, speed
	// compensation); sound effects need them (per-species cry pitch).
	if (s->sfx && mPlaying && bus32(s->gba, info) == mHeader) copyControls(s, main, info);
}

// --- comparisons ---------------------------------------------------------------

static double corr(const int16_t* a, const int16_t* b, size_t n) {
	double ab = 0, aa = 0, bb = 0;
	for (size_t i = 0; i < n; ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
	return aa && bb ? ab / sqrt(aa * bb) : 0;
}

static double rms(const int16_t* a, size_t n) {
	double s = 0;
	for (size_t i = 0; i < n; ++i) s += (double) a[i] * a[i];
	return n ? sqrt(s / n) : 0;
}

int main(int argc, char** argv) {
	if (argc < 3) return 2;
	mLogSetDefaultLogger(&logger);
	struct VFile* vf = VFileOpen(argv[1], O_RDONLY);
	romSize = vf->size(vf);
	rom = malloc(romSize);
	vf->read(vf, rom, romSize);
	vf->close(vf);

	struct M4a m;
	bool ok = findM4a(&m);
	printf("m4a %s: MPlayStop=%08X SoundMain=%08X VSync=%08X SongNumStart=%08X MPlayStart=%08X MPlayTable=%08X SongTable=%08X\n",
	       ok ? "found" : "MISSING", m.mplayStop, m.soundMain, m.soundVSync, m.songNumStart, m.mplayStart, m.mplayTable, m.songTable);
	if (!ok || !strcmp(argv[2], "find")) return ok ? 0 : 1;

	int warm = atoi(argv[3]), frames = atoi(argv[4]);
	int speed = argc > 5 ? atoi(argv[5]) : 1;
	struct mCore* main = makeCore();
	if (getenv("SAVE")) {
		struct VFile* sv = VFileOpen(getenv("SAVE"), O_RDONLY);
		size_t sz = sv->size(sv);
		void* buf = malloc(sz);
		sv->read(sv, buf, sz);
		sv->close(sv);
		main->loadSave(main, VFileFromMemory(buf, sz));
		main->reset(main);
	}
	// RTC follows the 1x wall clock (shadow frames), as on a device.
	int64_t wallMs = 1700000000000LL;
	main->rtc.override = RTC_FIXED;
	main->rtc.value = wallMs;
	int pressUntil = getenv("PRESS_UNTIL") ? atoi(getenv("PRESS_UNTIL")) : 1 << 30;
	unsigned rate = main->audioSampleRate(main);
	bool press = getenv("PRESS") != NULL;
	for (int i = 0; i < warm; ++i) {
		if (press) main->setKeys(main, i < pressUntil && (i % 90) < 4 ? (getenv("PRESS_A") ? 1 : 1 << 3 | 1) : 0);
		main->runFrame(main);
		observe(main->board);
		wallMs += 1000 / 60; main->rtc.value = wallMs;
		drain(main, NULL);
	}

	{ struct GBA* g = main->board; printf("savedata type=%d dirty=%d\n", g->memory.savedata.type, g->memory.savedata.dirty); }
	if (!strcmp(argv[2], "probe")) {
		struct GBA* g = main->board;
		uint32_t si = bus32(g, 0x03007FF0);
		printf("maxLines=%u mode=%02X pcmSamplesPerVBlank=%d\n", g->cpu->memory.load8(g->cpu, si + 0xC, NULL), g->cpu->memory.load8(g->cpu, si + 9, NULL), (int) bus32(g, si + 0x10));
		for (int f = 0; f < 5; ++f) {
			uint32_t start = g->video.frameCounter;
			while (g->video.frameCounter == start) {
				uint32_t pc = g->cpu->gprs[ARM_PC] - (g->cpu->executionMode == MODE_THUMB ? 2 : 4);
				if (pc == m.soundMain || pc == m.soundVSync) printf("  frame %d: %s at VCOUNT=%u\n", f, pc == m.soundMain ? "SoundMain" : "VSync", g->memory.io[GBA_REG(VCOUNT)]);
				ARMRun(g->cpu);
			}
		}
		return 0;
	}
	{ size_t nz = 0, h = 0; for (int i = 0; i < 256 * 160; ++i) { nz += video[i] != 0; h = h * 31 + video[i]; } struct GBA* g = main->board; printf("video nonzero=%zu hash=%zx pc=%08X halted=%d IE=%04X IME=%d\n", nz, h, g->cpu->gprs[ARM_PC], g->cpu->halted, g->memory.io[GBA_REG(IE)], g->memory.io[GBA_REG(IME)]); }
	if (getenv("TABLE")) {
		struct GBA* g = main->board;
		for (int pl = 0; pl < 12; ++pl) {
			uint32_t e = m.mplayTable + pl * 12;
			if (e >= m.songTable) break;
			uint32_t inf = bus32(g, e);
			printf("  mplay[%d] info=%08X tracks=%08X maxTracks=%u  header=%08X status=%08X\n", pl, inf, bus32(g, e + 4), g->cpu->memory.load8(g->cpu, e + 8, NULL), bus32(g, inf), bus32(g, inf + 4));
		}
	}
	// PREFF=a,b: before cloning, run `a` 1x-seconds at the target speed, then `b` 1x-frames at 1x (a release).
	if (getenv("PREFF")) {
		int a = 0, b = 0;
		sscanf(getenv("PREFF"), "%d,%d", &a, &b);
		struct GBA* g = main->board;
		for (int t = 0; t < a * 60 + b; ++t) {
			int n = t < a * 60 ? speed : 1;
			for (int k = 0; k < n; ++k) { main->runFrame(main); observe(main->board); drain(main, NULL); }
			wallMs += 1000 / 60; main->rtc.value = wallMs;
			if (t % 30 == 0) printf("  pre t=%.1fs speed=%d merc tempo=%u pitch=%d\n", t / 60.0, n, g->cpu->memory.load16(g->cpu, 0x0203B798, NULL), (int16_t) g->cpu->memory.load16(g->cpu, 0x0203B79A, NULL));
		}
	}
	if (!strcmp(argv[2], "stoptest")) {
		// Same clone, two ways to stop the music player; RMS of each 100 ms after.
		for (int way = 0; way < 2; ++way) {
			struct Shadow t = { 0 };
			shadowInit(&t, main, &m);
			for (int f = 0; f < 30; ++f) { shadowFrame(&t, &m); drain(t.core, NULL); }
			if (way == 0) {
				struct ARMCore* c = t.gba->cpu;
				c->memory.store32(c, t.bgmInfo + 4, bus32(t.gba, t.bgmInfo + 4) | 0x80000000, NULL);
			} else {
				shadowCall(t.gba, m.mplayStop, t.bgmInfo, 0, t.sp);
			}
			printf("%s:", way == 0 ? "pause-bit" : "MPlayStop");
			for (int w = 0; w < 10; ++w) {
				struct Pcm pc = { 0 };
				for (int f = 0; f < 6; ++f) { shadowFrame(&t, &m); drain(t.core, &pc); }
				printf(" %5.0f", rms(pc.s, pc.n * 2));
				if (w == 9 && getenv("DUMP")) { printf(" [%d %d %d %d ... bgm status=%08X]", pc.s[0], pc.s[1], pc.s[100], pc.s[101], bus32(t.gba, t.bgmInfo + 4)); }
				free(pc.s);
			}
			printf("\n");
		}
		return 0;
	}
	if (!strcmp(argv[2], "split")) {
		// Lockstep music twin + sfx shadow; main should be ~ their sum.
		struct Shadow tw = { 0 }, fx = { 0 };
		shadowInit(&tw, main, &m);
		shadowInit(&fx, main, &m);
		fx.sfx = true;
		// The sfx shadow starts without music (twin without sfx: the next sync stops them).
		struct Pcm mp = { 0 }, tp = { 0 }, xp = { 0 };
		FILE* kl = fopen("sfxkeys.log", "w");
		for (int f = 0; f < frames; ++f) {
			for (int k = 0; k < speed; ++k) {
				if (press) main->setKeys(main, ((f * speed + k) % 40) < 2 ? 1 : 0);
				main->runFrame(main);
				observe(main->board);
				drain(main, &mp);
				if (speed == 1) { shadowSync(&tw, main->board, &m); shadowFrame(&tw, &m); drain(tw.core, &tp); }
				shadowSync(&fx, main->board, &m); shadowFrame(&fx, &m); drain(fx.core, &xp);
				// pitch the sfx shadow actually applies on its playing non-music players
				for (int p = 0; p < playerCount; ++p) {
					uint32_t pi = fx.info[p]; if (!pi || isLearned(pi)) continue;
					uint32_t st = bus32(fx.gba, pi + 4); if (!(st & 0xFFFF) || (st & 0x80000000)) continue;
					uint32_t tr = bus32(fx.gba, pi + 0x2C);
					fprintf(kl, "%d p%d key=%d pit=%d mainTempo=%u\n", f, p, (int8_t) fx.gba->cpu->memory.load8(fx.gba->cpu, tr + 0x0B, NULL), (int8_t) fx.gba->cpu->memory.load8(fx.gba->cpu, tr + 0x0D, NULL), ((struct GBA*) main->board)->cpu->memory.load16(((struct GBA*) main->board)->cpu, 0x0203B798, NULL));
				}
			}
			wallMs += 1000 / 60; main->rtc.value = wallMs;
		}
		fclose(kl);
		printf("pitchSkipped=%u\n", pitchSkipped);
		if (speed > 1) { writeWav("split_sfx.wav", &xp, 65536); return 0; }
		size_t n = mp.n; if (tp.n < n) n = tp.n; if (xp.n < n) n = xp.n;
		int16_t* sum = malloc(n * 4);
		for (size_t i = 0; i < n * 2; ++i) { int v = tp.s[i] + xp.s[i]; sum[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
		printf("twin starts=%u stops=%u | sfx starts=%u stops=%u controls=%u ramHeaders=%u budgetHits=%lu\n", tw.starts, tw.stops, fx.starts, fx.stops, fx.controls, fx.ramHeaders, budgetHits);
		size_t W = 32768;
		if (getenv("NOANALYSIS")) n = 0;
		for (size_t st = 0; st + W + 4096 < n; st += W) {
			double bs = -2, bt = -2;
			for (int lag = -1300; lag <= 1300; lag += 1) {
				if ((long) st + lag < 0) continue;
				double cs = corr(mp.s + st * 2, sum + (st + lag) * 2, W * 2);
				double ct = corr(mp.s + st * 2, tp.s + (st + lag) * 2, W * 2);
				if (cs > bs) bs = cs;
				if (ct > bt) bt = ct;
			}
			printf("  t=%4.1fs rms main=%5.0f twin=%5.0f sfx=%5.0f corr(main, twin+sfx)=%.3f corr(main, twin)=%.3f\n", st / 65536.0, rms(mp.s + st * 2, W * 2), rms(tp.s + st * 2, W * 2), rms(xp.s + st * 2, W * 2), bs, bt);
		}
		writeWav("split_main.wav", &mp, 65536); writeWav("split_sfx.wav", &xp, 65536); writeWav("split_twin.wav", &tp, 65536);
		return 0;
	}
	struct Shadow s = { 0 };
	shadowInit(&s, main, &m);
	struct Pcm mainPcm = { 0 }, shadowPcm = { 0 };
	{ struct GBA* g = main->board; printf("masterVol=%d soundcntX=%04X cntH=%04X opts.vol=%d mute=%d\n", g->audio.masterVolume, g->memory.io[GBA_REG(SOUNDCNT_X)], g->memory.io[GBA_REG(SOUNDCNT_HI)], main->opts.volume, main->opts.mute); }
	printf("BGM info=%08X header=%08X status=%08X\n", s.bgmInfo, bus32(main->board, s.bgmInfo), bus32(main->board, s.bgmInfo + 4));

	for (int f = 0; f < frames; ++f) {
		for (int k = 0; k < speed; ++k) {
			if (press) main->setKeys(main, warm + f * speed + k < pressUntil && ((f * speed + k) % 90) < 4 ? (getenv("PRESS_A") ? 1 : 1 << 3 | 1) : 0);
			main->runFrame(main);
			observe(main->board);
			drain(main, &mainPcm);
		}
		wallMs += 1000 / 60; main->rtc.value = wallMs;
		if (!getenv("NOSHADOW")) {
			if (speed > 1) shadowSync(&s, main->board, &m);
			shadowFrame(&s, &m);
		}
		drain(s.core, &shadowPcm);
		if (speed == 1) {
			struct GBA* g = main->board;
			uint32_t si = bus32(g, 0x03007FF0), si2 = bus32(s.gba, 0x03007FF0);
			int diff = 0, firstDiff = -1;
			for (uint32_t o = 0; o < 0x350 + 1584 * 2; o += 4) {
				if (o >= 0x10 && o < 0x50) continue; // pointers/config
				if (bus32(g, si + o) != bus32(s.gba, si2 + o)) { ++diff; if (firstDiff < 0) firstDiff = o; }
			}
			uint8_t c1 = g->cpu->memory.load8(g->cpu, si + 4, NULL), c2 = s.gba->cpu->memory.load8(s.gba->cpu, si2 + 4, NULL);
			int pd = 0;
			for (int pl = 0; pl < 4; ++pl) {
				uint32_t inf = bus32(g, m.mplayTable + pl * 12), trk = bus32(g, m.mplayTable + pl * 12 + 4);
				uint8_t nt = g->cpu->memory.load8(g->cpu, m.mplayTable + pl * 12 + 8, NULL);
				int d = 0;
				for (uint32_t o = 0; o < 0x40; o += 4) d += bus32(g, inf + o) != bus32(s.gba, inf + o);
				for (uint32_t o = 0; o < nt * 0x50u; o += 4) d += bus32(g, trk + o) != bus32(s.gba, trk + o);
				if (f < 5 || f % 60 == 0) printf("    player%d info=%08X status main=%08X shadow=%08X diffWords=%d\n", pl, inf, bus32(g, inf + 4), bus32(s.gba, inf + 4), d);
				pd += d;
			}
			if (f < 5 || (f % 60 == 0)) printf("  f=%d SoundInfo diff words=%d first=%X dmaCounter main=%u shadow=%u\n", f, diff, firstDiff, c1, c2);
		}
		if (speed > 1 && f % 60 == 0) {
			printf("t=%3ds", f / 60);
			if (getenv("MERC")) { struct GBA* g = main->board; printf(" merc tempo=%u pitch=%d", g->cpu->memory.load16(g->cpu, 0x0203B798, NULL), (int16_t) g->cpu->memory.load16(g->cpu, 0x0203B79A, NULL)); }
			for (int p = 0; p < playerCount; ++p) {
				if (!s.info[p]) continue;
				uint32_t st = bus32(main->board, s.info[p] + 4), h = bus32(main->board, s.info[p]);
				uint32_t st2 = bus32(s.gba, s.info[p] + 4), h2 = bus32(s.gba, s.info[p]);
				bool pl = (st & 0xFFFF) && !(st & 0x80000000), pl2 = (st2 & 0xFFFF) && !(st2 & 0x80000000);
				uint16_t tm = main->board ? ((struct GBA*) main->board)->cpu->memory.load16(((struct GBA*) main->board)->cpu, s.info[p] + 0x1E, NULL) : 0;
				uint16_t ts = s.gba->cpu->memory.load16(s.gba->cpu, s.info[p] + 0x1E, NULL);
				if (pl || pl2) printf(" p%d[main %s%08X%s tempoU=%u | shadow %s%08X tempoU=%u]", p, pl ? "" : "-", h, songLoopsCached(h) ? "L" : "", tm, pl2 ? "" : "-", h2, ts);
			}
			printf("\n");
		}
	}
	printf("rate=%u main=%zu shadow=%zu frames, starts=%u stops=%u continues=%u fades=%u budgetHits=%lu\n", rate, mainPcm.n, shadowPcm.n, s.starts, s.stops, s.continues, s.fades, budgetHits);
	size_t n = (mainPcm.n < shadowPcm.n ? mainPcm.n : shadowPcm.n) * 2;
	if (speed == 1) {
		// Best lag within +-2048 samples.
		double best = -2; int bestLag = 0;
		for (int lag = -2048; lag <= 2048; lag += 2) {
			size_t off = (size_t) abs(lag), len = n - off - 1;
			double c = lag >= 0 ? corr(mainPcm.s + off, shadowPcm.s, len) : corr(mainPcm.s, shadowPcm.s + off, len);
			if (c > best) { best = c; bestLag = lag; }
		}
		printf("rms main=%.0f shadow=%.0f corr@lag0=%.5f best=%.5f at lag %d samples\n", rms(mainPcm.s, n), rms(shadowPcm.s, n),
		       corr(mainPcm.s, shadowPcm.s, n), best, bestLag / 2);
	}
	if (speed == 1) {
		size_t W = rate; // 1 s of stereo frames
		for (size_t st = 0; st + W + 4096 < n / 2; st += W) {
			double best = -2; int bl = 0;
			for (int lag = -2400; lag <= 2400; ++lag) {
				if ((long) st + lag < 0) continue;
				double c = corr(mainPcm.s + st * 2, shadowPcm.s + (st + lag) * 2, W * 2);
				if (c > best) { best = c; bl = lag; }
			}
			printf("  t=%2zus rms main=%5.0f shadow=%5.0f corr=%.4f lag=%d\n", st / W, rms(mainPcm.s + st * 2, W * 2), rms(shadowPcm.s + st * 2, W * 2), best, bl);
		}
	}
	if (speed > 1) { size_t sn = shadowPcm.n * 2, sil = 0; for (size_t i = 0; i + 2048 < sn; i += 2048) sil += rms(shadowPcm.s + i, 2048) < 50; printf("shadow rms=%.0f silentChunks=%zu/%zu main rms=%.0f\n", rms(shadowPcm.s, sn), sil, sn / 2048, rms(mainPcm.s, mainPcm.n * 2)); }
	if (getenv("SHOT")) {
		FILE* f = fopen(getenv("SHOT"), "wb");
		fprintf(f, "P6 240 160 255\n");
		for (int y = 0; y < 160; ++y) for (int x = 0; x < 240; ++x) {
			uint16_t c = video[y * 256 + x];
			uint8_t rgb[3] = { (uint8_t) ((c >> 11) << 3), (uint8_t) (((c >> 5) & 63) << 2), (uint8_t) ((c & 31) << 3) };
			fwrite(rgb, 1, 3, f);
		}
		fclose(f);
	}
	writeWav("main.wav", &mainPcm, 65536);
	writeWav("shadow.wav", &shadowPcm, 65536);
	return 0;
}
