#!/bin/bash

# 构建 Android ARMv8 (arm64-v8a) 的 libretro.so
# 使用方法: ./build_android_arm64.sh [NDK路径] [--copy] [--no-copy]
# 或者设置环境变量: export ANDROID_NDK=/path/to/android-ndk

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 解析参数
AUTO_COPY_FLAG=""
NO_COPY_FLAG=""
NDK_PATH=""

for arg in "$@"; do
    case "$arg" in
        --copy)
            AUTO_COPY_FLAG="--copy"
            ;;
        --no-copy)
            NO_COPY_FLAG="--no-copy"
            ;;
        *)
            # 如果不是选项，则认为是 NDK 路径
            if [ -z "$NDK_PATH" ] && [ ! -z "$arg" ]; then
                NDK_PATH="$arg"
            fi
            ;;
    esac
done

# 确定 NDK 路径
if [ -z "$NDK_PATH" ]; then
    if [ -n "$ANDROID_NDK" ]; then
        NDK_PATH="$ANDROID_NDK"
    elif [ -n "$NDK_ROOT" ]; then
        NDK_PATH="$NDK_ROOT"
    else
        echo "错误: 请提供 Android NDK 路径"
        echo "使用方法: $0 [NDK路径] [--copy] [--no-copy]"
        echo "或者设置环境变量: export ANDROID_NDK=/path/to/android-ndk"
        echo ""
        echo "选项:"
        echo "  --copy              - 构建完成后自动复制库文件到项目目录"
        echo "  --no-copy           - 不显示复制提示"
        exit 1
    fi
fi

# 检查 NDK 是否存在
if [ ! -d "$NDK_PATH" ]; then
    echo "错误: NDK 路径不存在: $NDK_PATH"
    exit 1
fi

# 检查 ndk-build 是否存在
NDK_BUILD="$NDK_PATH/ndk-build"
if [ ! -f "$NDK_BUILD" ]; then
    echo "错误: 找不到 ndk-build: $NDK_BUILD"
    exit 1
fi

echo "使用 NDK: $NDK_PATH"
echo "开始构建 Android ARMv8 (arm64-v8a) 的 libretro.so..."
echo ""

# 设置环境变量并运行 ndk-build
export ANDROID_NDK="$NDK_PATH"
export NDK_ROOT="$NDK_PATH"

# 清理之前的构建（可选）
# "$NDK_BUILD" clean

# 构建
"$NDK_BUILD" -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

echo ""
echo "构建完成！"
echo "输出文件应该在: libs/arm64-v8a/libretro.so"

# 如果设置了 AUTO_COPY 环境变量，或者提供了 --copy 参数，则自动复制
if [[ "$AUTO_COPY" == "1" ]] || [[ -n "$AUTO_COPY_FLAG" ]]; then
    echo ""
    echo "自动复制库文件到 Android 项目目录..."
    if [ -f "$SCRIPT_DIR/copy_libs.sh" ]; then
        "$SCRIPT_DIR/copy_libs.sh" android
    else
        echo "警告: 找不到 copy_libs.sh 脚本"
    fi
elif [[ -z "$NO_COPY_FLAG" ]]; then
    echo ""
    echo "提示: 使用 './copy_libs.sh android' 复制库文件到项目目录"
    echo "     或运行构建脚本时添加 --copy 参数自动复制"
fi

