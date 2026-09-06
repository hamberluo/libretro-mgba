#!/bin/bash

# 在宿主机上编译并运行 GB core 的回归测试。
#
# 上游 mgba 的测试套件走 CMake + cmocka，而那套基建（src/platform/test、
# cmocka）在本 fork 精简时被移除了，且与我们实际发版用的 ndk-build /
# build_ios.sh 是两条独立路径。这里直接用真实源码编一个宿主可执行文件：
# 不需要 cmake，不需要 cmocka，一条命令就能跑。
#
# 用法: ./run_tests.sh [测试名...]
#       不带参数则运行全部。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/test-build"

CC="${CC:-cc}"

# 与 Makefile.common 的 RETRODEFS 保持一致，否则头文件里的条件编译会走到
# 和实际 core 不同的分支上，测出来的就不是发版的那份代码。
DEFINES=(
	-D__LIBRETRO__
	-DMINIMAL_CORE=2
	-DM_CORE_GB
	-DM_CORE_GBA
	-DENABLE_VFS
	-DENABLE_VFS_FILE
	-DENABLE_DIRECTORIES
	-DDISABLE_THREADING
	-DHAVE_STRLCPY
	-DHAVE_STDINT_H
	-DHAVE_INTTYPES_H
	-DHAVE_LOCALTIME_R
	-DINLINE=inline
	-DCOLOR_16_BIT
	-DRESAMPLE_LIBRARY=2
	-DM_PI=3.14159265358979323846
	-DMGBA_STANDALONE
	-DPATH_MAX=1024
)

INCLUDES=(-I"$ROOT_DIR/include" -I"$ROOT_DIR/src" -I"$ROOT_DIR")

# 每项测试: 名字:测试源文件:一起编译的真实源文件(空格分隔)
TESTS=(
	"mbc-sram:src/gb/test/mbc-sram.c:src/gb/mbc.c src/gb/mbc/huc-3.c src/gb/mbc/licensed.c src/gb/mbc/mbc.c src/gb/mbc/pocket-cam.c src/gb/mbc/tama5.c src/gb/mbc/unlicensed.c src/util/vfs/vfs-mem.c src/util/crc32.c"
)

mkdir -p "$BUILD_DIR"

selected=("$@")
ran=0
failed=0

for entry in "${TESTS[@]}"; do
	name="${entry%%:*}"
	rest="${entry#*:}"
	test_src="${rest%%:*}"
	core_src="${rest#*:}"

	if [ ${#selected[@]} -gt 0 ]; then
		match=0
		for want in "${selected[@]}"; do
			[ "$want" = "$name" ] && match=1
		done
		[ $match -eq 0 ] && continue
	fi

	echo "========================================"
	echo "$name"
	echo "========================================"

	# shellcheck disable=SC2086
	"$CC" -g -O1 -Wall \
		"${DEFINES[@]}" "${INCLUDES[@]}" \
		-o "$BUILD_DIR/$name" \
		"$ROOT_DIR/$test_src" \
		"$ROOT_DIR/src/gb/test/stubs.c" \
		$(cd "$ROOT_DIR" && ls $core_src | sed "s|^|$ROOT_DIR/|")

	ran=$((ran + 1))
	if "$BUILD_DIR/$name"; then
		echo ""
	else
		failed=$((failed + 1))
		echo ""
	fi
done

echo "========================================"
if [ $ran -eq 0 ]; then
	echo "没有匹配的测试"
	exit 1
elif [ $failed -eq 0 ]; then
	echo "全部通过 ($ran 项)"
else
	echo "$failed / $ran 项失败"
	exit 1
fi
