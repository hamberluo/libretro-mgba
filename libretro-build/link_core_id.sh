#!/bin/sh
# Prints the link compatibility fingerprint: 16 hex digits of a SHA-1 over
# every source and header that decides emulated behaviour, plus
# Makefile.common (it carries -fwrapv). Two cores link only if these match.
#
# Deliberately not the git commit -- most commits here are docsite-only -- and
# not the platform Makefiles or RETRODEFS, which differ between iOS
# (HAVE_STRLCPY) and Android (ENABLE_VFS_FD) by design.
#
# usage: link_core_id.sh <libretro-mgba root>
set -e
cd "$1"
{
	find src/arm src/sm83 src/gba src/gb src/core src/util src/platform/libretro include \
		\( -name '*.c' -o -name '*.h' \) -not -path '*/test/*'
	echo libretro-build/Makefile.common
} | LC_ALL=C sort | while IFS= read -r f; do
	printf '%s\n' "$f"
	cat "$f"
done | { shasum -a 1 2>/dev/null || sha1sum; } | cut -c1-16
