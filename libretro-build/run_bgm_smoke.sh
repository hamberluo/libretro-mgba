#!/bin/bash
# Builds tools/bgm-smoke.c (GBA) and tools/bgm-smoke-gb.c (GB/GBC) and runs one
# mode over the developer's own ROMs (never committed), each with its tool.
# usage: ./run_bgm_smoke.sh <mode> [warm frames speed]
#   BUILD_ONLY=1 only builds libretro-build/test-build/bgm-smoke{,-gb}.
#   ROM_DIR overrides ~/Documents/game; BIOS defaults to its <platform>/<platform>_bios.bin
#   (BIOS= runs without); other knobs are the tools' env vars.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/test-build"
ROM_DIR="${ROM_DIR:-$HOME/Documents/game}"
mkdir -p "$BUILD_DIR"
build() { # tool source, output
(cd "$ROOT_DIR" && cc -g -O2 -fwrapv -w -D__LIBRETRO__ -DMINIMAL_CORE=2 -DM_CORE_GB -DM_CORE_GBA -DENABLE_VFS \
	-DENABLE_VFS_FILE -DENABLE_DIRECTORIES -DDISABLE_THREADING -DHAVE_STRLCPY -DHAVE_STDINT_H \
	-DHAVE_INTTYPES_H -DHAVE_LOCALTIME_R -DINLINE=inline -DCOLOR_16_BIT -DCOLOR_5_6_5 \
	-DRESAMPLE_LIBRARY=2 -DM_PI=3.14159265358979323846 -DMGBA_STANDALONE -DPATH_MAX=1024 \
	-DHAVE_LOCALE -DHAVE_XLOCALE -DHAVE_STRTOF_L -Iinclude -Isrc -I. \
	-o "$BUILD_DIR/$2" "libretro-build/tools/$1" src/gba/test/core-deinit-stubs.c \
	src/platform/libretro/aux-core.c src/platform/libretro/realtime-bgm.c src/platform/libretro/realtime-bgm-m4a.c src/platform/libretro/realtime-bgm-stream.c src/platform/libretro/realtime-bgm-locate.c src/platform/libretro/realtime-bgm-gb.c src/platform/libretro/realtime-bgm-gb-locate.c \
	src/core/*.c src/gba/*.c src/gba/renderers/*.c src/gba/cheats/*.c \
	src/gba/cart/*.c src/gba/sio/*.c src/arm/*.c src/gb/*.c src/gb/mbc/*.c src/gb/sio/*.c \
	src/gb/renderers/*.c src/sm83/*.c src/util/*.c src/util/vfs/vfs-file.c src/util/vfs/vfs-mem.c -lm)
}
build bgm-smoke.c bgm-smoke
build bgm-smoke-gb.c bgm-smoke-gb
[ -n "$BUILD_ONLY" ] && exit 0
mode="${1:-find}"
shift || true
find "$ROM_DIR" \( -name '*.gba' -o -name '*.gb' -o -name '*.gbc' \) | LC_ALL=C sort | while IFS= read -r rom; do
	tool=bgm-smoke bios=gba/gba_bios.bin
	case "$rom" in *.gb) tool=bgm-smoke-gb bios=gb/gb_bios.bin ;; *.gbc) tool=bgm-smoke-gb bios=gbc/gbc_bios.bin ;; esac
	printf '%s: ' "$(basename "$rom")"
	# Devices boot the real BIOS: bugs hid where the smoke skipped it.
	BIOS="${BIOS-$ROM_DIR/$bios}" "$BUILD_DIR/$tool" "$rom" "$mode" "$@" | head -1
done
