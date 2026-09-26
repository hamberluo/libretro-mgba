#!/bin/bash
# Builds and runs tools/link-smoke.c. ROMs are the developer's own and never
# committed. usage: ./run_link_smoke.sh rom.gba script.txt
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/test-build"
mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"
cc -g -O2 -fwrapv -w -D__LIBRETRO__ -DMINIMAL_CORE=2 -DM_CORE_GB -DM_CORE_GBA -DENABLE_VFS \
	-DENABLE_VFS_FILE -DENABLE_DIRECTORIES -DDISABLE_THREADING -DHAVE_STRLCPY -DHAVE_STDINT_H \
	-DHAVE_INTTYPES_H -DHAVE_LOCALTIME_R -DINLINE=inline -DCOLOR_16_BIT -DCOLOR_5_6_5 \
	-DRESAMPLE_LIBRARY=2 -DM_PI=3.14159265358979323846 -DMGBA_STANDALONE -DPATH_MAX=1024 \
	-DHAVE_LOCALE -DHAVE_XLOCALE -DHAVE_STRTOF_L -Iinclude -Isrc -I. \
	-o "$BUILD_DIR/link-smoke" libretro-build/tools/link-smoke.c src/gba/test/core-deinit-stubs.c \
	src/platform/libretro/link.c src/core/*.c src/gba/*.c src/gba/renderers/*.c src/gba/cheats/*.c \
	src/gba/cart/*.c src/gba/sio/*.c src/arm/*.c src/gb/*.c src/gb/mbc/*.c src/gb/sio/*.c \
	src/gb/renderers/*.c src/sm83/*.c src/util/*.c src/util/vfs/vfs-file.c src/util/vfs/vfs-mem.c
exec "$BUILD_DIR/link-smoke" "$1" "$2"
