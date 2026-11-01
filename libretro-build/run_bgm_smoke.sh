#!/bin/bash
# Builds tools/bgm-smoke.c and runs one mode over the developer's own GBA ROMs
# (never committed). usage: ./run_bgm_smoke.sh <mode> [warm frames speed]
#   BUILD_ONLY=1 only builds libretro-build/test-build/bgm-smoke.
#   ROM_DIR overrides ~/Documents/game/gba; other knobs are bgm-smoke's env vars.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/test-build"
ROM_DIR="${ROM_DIR:-$HOME/Documents/game/gba}"
mkdir -p "$BUILD_DIR"
(cd "$ROOT_DIR" && cc -g -O2 -fwrapv -w -D__LIBRETRO__ -DMINIMAL_CORE=2 -DM_CORE_GB -DM_CORE_GBA -DENABLE_VFS \
	-DENABLE_VFS_FILE -DENABLE_DIRECTORIES -DDISABLE_THREADING -DHAVE_STRLCPY -DHAVE_STDINT_H \
	-DHAVE_INTTYPES_H -DHAVE_LOCALTIME_R -DINLINE=inline -DCOLOR_16_BIT -DCOLOR_5_6_5 \
	-DRESAMPLE_LIBRARY=2 -DM_PI=3.14159265358979323846 -DMGBA_STANDALONE -DPATH_MAX=1024 \
	-DHAVE_LOCALE -DHAVE_XLOCALE -DHAVE_STRTOF_L -Iinclude -Isrc -I. \
	-o "$BUILD_DIR/bgm-smoke" libretro-build/tools/bgm-smoke.c src/gba/test/core-deinit-stubs.c \
	src/platform/libretro/aux-core.c src/platform/libretro/realtime-bgm.c src/platform/libretro/realtime-bgm-locate.c \
	src/core/*.c src/gba/*.c src/gba/renderers/*.c src/gba/cheats/*.c \
	src/gba/cart/*.c src/gba/sio/*.c src/arm/*.c src/gb/*.c src/gb/mbc/*.c src/gb/sio/*.c \
	src/gb/renderers/*.c src/sm83/*.c src/util/*.c src/util/vfs/vfs-file.c src/util/vfs/vfs-mem.c -lm)
[ -n "$BUILD_ONLY" ] && exit 0
mode="${1:-find}"
shift || true
find "$ROM_DIR" -name '*.gba' | LC_ALL=C sort | while IFS= read -r rom; do
	printf '%s: ' "$(basename "$rom")"
	"$BUILD_DIR/bgm-smoke" "$rom" "$mode" "$@" | head -1
done
