#!/bin/bash

# Runs one real ROM through the enable/disable cycle a frontend performs when
# a cheat is switched off mid-game, and reports what the ROM patch and the RAM
# write look like on each side of it.
#
# Unlike run_tests.sh this needs a real ROM and links the whole GBA core, so it
# is a diagnostic you point at a game, not a self-contained regression test.
#
# Usage: ./run_cheat_rom_toggle.sh <rom.gba>

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/test-build"

if [ $# -lt 1 ]; then
	echo "usage: $0 <rom.gba>"
	exit 2
fi

CC="${CC:-cc}"
mkdir -p "$BUILD_DIR"

# Same RETRODEFS as run_tests.sh, so this exercises the shipping branches.
DEFINES=(
	-D__LIBRETRO__ -DMINIMAL_CORE=2 -DM_CORE_GBA -DENABLE_VFS -DENABLE_VFS_FILE
	-DENABLE_DIRECTORIES -DDISABLE_THREADING -DHAVE_STRLCPY -DHAVE_STDINT_H
	-DHAVE_INTTYPES_H -DHAVE_LOCALTIME_R -DINLINE=inline -DCOLOR_16_BIT
	-DRESAMPLE_LIBRARY=2 -DM_PI=3.14159265358979323846 -DMGBA_STANDALONE
	-DHAVE_XLOCALE -DPATH_MAX=1024 ${EXTRA_DEFINES}
)

cd "$ROOT_DIR"
SRCS=$(ls src/gba/*.c src/gba/cart/*.c src/gba/cheats/*.c src/gba/renderers/*.c \
	src/gba/sio/gbp.c src/arm/*.c src/core/*.c src/util/*.c src/util/vfs/vfs-mem.c \
	src/util/vfs/vfs-file.c src/gb/audio.c src/platform/posix/memory.c \
	src/third-party/inih/ini.c)

# projectName/projectVersion normally come from the generated version file.
VERSION_STUB="$BUILD_DIR/version-stub.c"
cat > "$VERSION_STUB" <<'EOC'
const char* const projectName = "mGBA";
const char* const projectVersion = "0";
EOC

# shellcheck disable=SC2086
"$CC" -g -O1 -w "${DEFINES[@]}" -Iinclude -Isrc -I. \
	-o "$BUILD_DIR/cheat-rom-toggle" \
	src/platform/libretro/test/cheat-rom-toggle.c $SRCS "$VERSION_STUB"

# The core logs heavily while booting a real game; the verdict lines are what
# this script is for.
"$BUILD_DIR/cheat-rom-toggle" "$1" 2>&1 \
	| grep -E "ram cheat|rom patch|before enable|with cheat on|after mCheat|after enabled|after 60|ROM DIFF|whole-ROM|^  ok|^  FAIL|checks,"
