---
description: Review new commits from upstream mgba and libretro/mgba, and merge the ones that matter to this libretro-only fork
argument-hint: "[report]  (report = list candidates only, merge nothing)"
---

Sync this fork with its two upstreams. This fork is a trimmed, libretro-only mGBA: Qt/SDL/3DS/Vita, Lua scripting, the gdb debugger, rewind and more are removed, so most upstream commits do not apply. Upstream code is not a trusted baseline — this fork already fixes several bugs upstream still has. Cherry-pick by hunk and audit each one. Never overwrite whole files.

If `$ARGUMENTS` is `report`, stop after step 4.

## 1. Fetch

The last commit reviewed on each upstream is in `libretro-build/upstream_baseline.txt`.

```sh
git -C ../mgba fetch origin            # mgba-emu/mgba; if ../mgba is missing, fetch https://github.com/mgba-emu/mgba.git into refs/remotes/mgba/master
git fetch https://github.com/libretro/mgba.git master:refs/remotes/libretro/master
```

## 2. List candidates

- **mgba**: `git -C ../mgba log --date=short --format="%h %ad %an %s" <base>..origin/master -- src/gb src/gba src/arm src/sm83 src/core src/util include/mgba include/mgba-util src/platform/libretro`
- **libretro**: `git log --date=short --format="%h %ad %an %s" <base>..libretro/master -- src/platform/libretro libretro-build Makefile.libretro`. The rest of libretro/mgba is merges of mgba master, which the mgba list already covers.

## 3. Triage each candidate

For every commit, decide one of: **already here**, **merge**, **skip (does not apply)**, **reject (upstream bug)**.

- **Already here?** Compare the hunk with the current file (`grep` the changed lines). This fork often has the fix already, sometimes a stronger one.
- **Is the file built?** Only files in `SOURCES_C` in `libretro-build/Makefile.common` ship. Ignore stray `.o` files — they are old iOS/Android outputs. Code under `#ifndef DISABLE_THREADING`, `USE_PNG`, `ENABLE_SCRIPTING`, or `USE_DEBUGGERS` never builds here.
- **Is it reached?** Trace callers (use CodeGraph). An example: libretro runs with `sync == NULL`, so `mCoreSync*` returns immediately, but `src/util/interpolator.c` (sinc) is live through `mAudioResampler`.
- **Audit the upstream hunk itself.** Past upstream bugs include: an unreset `bankStart` in `543a19758`, the wrong-size bounds check in `405021552` (`inSize` vs `outSize`), and the sinc rewrite `c65e8a3d4` (offset divided by π, `sampleStep` ignored, no normalisation — rejected). Fix bugs like these while merging, or reject the commit.
- The fork's layout differs from upstream: GB MBCs are split into `src/gb/mbc/`. Port hunks by hand.

### Fixes this fork has and upstream does not — never revert these

- `src/gb/gb.c` `GBResizeSram`: NULL guard on `vf->map()`.
- `src/gb/mbc.c` `GBMBCSwitchSramBank` / `HalfBank`: NULL/zero-size clearing, pinning saves smaller than one bank, and falling back to bank 0 (with `bankStart = 0`).
- `src/util/vfs/vfs-mem.c` `_vfmMap`: the bound stays strict `>`.
- `src/gba/overrides.c`: Pokemon title strncmp lengths 19/23 (upstream 20/24) plus the extra `"POKEMON "` prefix match.
- `src/gba/bios.c`: the integer `_BgAffineSet` / `_ObjAffineSet` with the hardware sine table (upstream uses `sinf/cosf`).
- `src/gb/sio/lockstep.c`: the parked retry uses `GB_SIO_LOCKSTEP_RECHECK` and counts toward `eventDiff`.
- `libretro-build/Makefile.common`: `-fwrapv` in `RETRODEFS`. Lockstep link hangs without it.
- `src/util/patch-ups.c`: the BPS `TargetCopy` check uses `outSize`. `SourceRead` using `inSize` is correct.
- `src/platform/libretro/`: all link mode (`link.{c,h}`, `libretro_link.h`, `retro_link_*`), GB RTC persisted through SAVE_RAM, on-demand MBC6 allocation, and camera reallocation with pitch.

### Standing decisions — do not re-litigate without a new reason

- libretro/mgba `517e518c4` (linear resampler), `d030c2aa0` (RetroArch VFS), and CMake / file-rename / CI reshuffles: not merged.
- PNG fixes: skipped while `USE_PNG` is off.
- Leftover rewind declarations (`include/mgba/core/rewind.h`, `config.h` `rewind*`) stay. Rewind in libretro is the frontend's job, done through `retro_serialize`.

## 4. Report

Before changing anything, show one table per upstream: commit, one-line summary, verdict, reason. List the merge set. For `report`, stop here. Otherwise continue straight on — the user has asked for the sync.

## 5. Merge

1. Branch first: `git switch -c sync/upstream-<YYYY-MM-DD>`.
2. For each change that has a testable effect, add a failing case under `src/gb/test/`, `src/gba/test/`, or `src/platform/libretro/test/` (wired in `libretro-build/run_tests.sh`), watch it fail, then port the hunk. Keep the fork's code style: little commenting, self-explanatory code, only the tests that are needed.
3. One commit per logical change. Credit the source in the body, e.g. `From upstream a95a688af.` or `From libretro/mgba 7a12d6d4b.`

## 6. Verify

- `libretro-build/run_tests.sh` — the whole suite, including `link-compat`.
- Compile each touched file on its own: `rm -f <file>.o && make -f Makefile.libretro <file>.o`. A full `make` fails on macOS in `src/core/config.c` (`locale_t`). That failure is the host environment, not a regression.

## 7. Link id

Any source edit changes the hash printed by `sh libretro-build/link_core_id.sh .`. If `link-compat` passes and nothing merged changes 2P link emulation, replace the mapping line in `libretro-build/link_compat.txt`: `<new hash> <shipped id>`, with a comment saying why. Otherwise leave the line out, and tell the user that iOS and Android must ship together. Say which choice you made. The hash covers `src/`, `include/`, and `Makefile.common`, so compute it after the last source edit.

## 8. Finish

- Update `libretro-build/upstream_baseline.txt` to each upstream's HEAD that was reviewed. Do this even for commits that were skipped, so they are not reviewed again. Commit it with the sync.
- If a new fork-only fix or standing decision came up, add it to the lists above in this file.
- Report the commits made, the verification results, and the link-id decision. Do not push unless asked.
