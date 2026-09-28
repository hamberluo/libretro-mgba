#!/bin/sh
# Prints the link compatibility fingerprint: 16 hex digits of a SHA-1 over
# every source and header that decides emulated behaviour, plus
# Makefile.common (it carries -fwrapv), or the shipped id link_compat.txt
# maps that hash to. Two cores link only if these match.
#
# Deliberately not the git commit -- most commits here are docsite-only -- and
# not the platform Makefiles or RETRODEFS, which differ between iOS
# (HAVE_STRLCPY) and Android (ENABLE_VFS_FD) by design.
#
# usage: link_core_id.sh <libretro-mgba root>
set -e
cd "$1"
id=$({
	find src/arm src/sm83 src/gba src/gb src/core src/util src/platform/libretro include \
		\( -name '*.c' -o -name '*.h' \) -not -path '*/test/*' -not -name link_core_id.h
	echo libretro-build/Makefile.common
} | LC_ALL=C sort | while IFS= read -r f; do
	printf '%s\n' "$f"
	cat "$f"
done | { shasum -a 1 2>/dev/null || sha1sum; } | cut -c1-16)
shipped=$(awk -v id="$id" '$1 == id { print $2 }' libretro-build/link_compat.txt)
echo "${shipped:-$id}"
