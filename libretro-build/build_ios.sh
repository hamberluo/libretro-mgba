#!/bin/bash

# 构建 iOS 的 libretro 库
# 支持真机 (arm64) 和模拟器 (arm64)
# iOS 15.0+
# 使用方法: ./build_ios.sh [target]
# target 可以是: device, simulator, simulator_arm64, all
# 默认: all

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查是否在 macOS 上
if [[ "$(uname)" != "Darwin" ]]; then
    echo "错误: iOS 构建只能在 macOS 系统上进行"
    exit 1
fi

# 检查 Xcode 是否安装
if ! command -v xcrun &> /dev/null; then
    echo "错误: 找不到 xcrun，请确保已安装 Xcode"
    exit 1
fi

# 检查 iOS SDK
IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null || echo "")
if [[ -z "$IOS_SDK" ]]; then
    echo "错误: 找不到 iOS SDK，请确保已安装 Xcode 和 iOS SDK"
    exit 1
fi

# 检查 iOS Simulator SDK
IOS_SIM_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null || echo "")
if [[ -z "$IOS_SIM_SDK" ]]; then
    echo "错误: 找不到 iOS Simulator SDK，请确保已安装 Xcode 和 iOS Simulator SDK"
    exit 1
fi

echo "iOS SDK: $IOS_SDK"
echo "iOS Simulator SDK: $IOS_SIM_SDK"
echo ""

# 解析参数
TARGET="all"
AUTO_COPY_FLAG=""
NO_COPY_FLAG=""

for arg in "$@"; do
    case "$arg" in
        --copy)
            AUTO_COPY_FLAG="--copy"
            ;;
        --no-copy)
            NO_COPY_FLAG="--no-copy"
            ;;
        device|simulator|simulator_arm64|all)
            TARGET="$arg"
            ;;
        *)
            # 忽略未知参数
            ;;
    esac
done

# 获取 CPU 核心数
CPU_CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

# 构建函数
build_target() {
    local target_name=$1
    local makefile=$2
    
    echo "=========================================="
    echo "构建 $target_name..."
    echo "=========================================="
    
    if [[ ! -f "$makefile" ]]; then
        echo "错误: 找不到 Makefile: $makefile"
        return 1
    fi
    
    make -f "$makefile" -j"$CPU_CORES" clean 2>/dev/null || true
    make -f "$makefile" -j"$CPU_CORES"
    
    if [[ $? -eq 0 ]]; then
        echo "✓ $target_name 构建成功"
        echo ""
        return 0
    else
        echo "✗ $target_name 构建失败"
        echo ""
        return 1
    fi
}

# 根据目标构建
case "$TARGET" in
    device)
        build_target "iOS arm64 (真机)" "Makefile.ios_arm64"
        ;;
    simulator)
        # 构建 arm64 模拟器
        build_target "iOS arm64 (模拟器)" "Makefile.ios_arm64_simulator"
        ;;
    simulator_arm64)
        build_target "iOS arm64 (模拟器)" "Makefile.ios_arm64_simulator"
        ;;
    all)
        echo "构建所有 iOS 目标..."
        echo ""
        
        # 构建真机
        build_target "iOS arm64 (真机)" "Makefile.ios_arm64"
        
        # 构建模拟器
        build_target "iOS arm64 (模拟器)" "Makefile.ios_arm64_simulator"
        
        echo "=========================================="
        echo "所有构建完成！"
        echo "=========================================="
        echo ""
        echo "输出文件:"
        echo "  真机: libs/ios-arm64/libretro.dylib"
        echo "  模拟器 arm64: libs/ios-simulator-arm64/libretro.dylib"
        
        # 如果设置了 AUTO_COPY 环境变量，或者提供了 --copy 参数，则自动复制
        if [[ "$AUTO_COPY" == "1" ]] || [[ -n "$AUTO_COPY_FLAG" ]]; then
            echo ""
            echo "自动复制库文件到 iOS 项目目录..."
            if [ -f "$SCRIPT_DIR/copy_libs.sh" ]; then
                "$SCRIPT_DIR/copy_libs.sh" ios
            else
                echo "警告: 找不到 copy_libs.sh 脚本"
            fi
        elif [[ -z "$NO_COPY_FLAG" ]]; then
            echo ""
            echo "提示: 使用 './copy_libs.sh ios' 复制库文件到项目目录"
            echo "     或运行构建脚本时添加 --copy 参数自动复制"
        fi
        ;;
    *)
        echo "用法: $0 [target] [--copy] [--no-copy]"
        echo ""
        echo "target 可以是:"
        echo "  device              - 构建真机版本 (arm64)"
        echo "  simulator           - 构建模拟器版本 (arm64)"
        echo "  simulator_arm64     - 构建 arm64 模拟器版本"
        echo "  all                 - 构建所有版本 (默认)"
        echo ""
        echo "选项:"
        echo "  --copy              - 构建完成后自动复制库文件到项目目录"
        echo "  --no-copy           - 不显示复制提示"
        exit 1
        ;;
esac

# 对于非 all 目标，也检查是否需要复制
if [[ "$TARGET" != "all" ]]; then
    if [[ "$AUTO_COPY" == "1" ]] || [[ -n "$AUTO_COPY_FLAG" ]]; then
        echo ""
        echo "自动复制库文件到 iOS 项目目录..."
        if [ -f "$SCRIPT_DIR/copy_libs.sh" ]; then
            "$SCRIPT_DIR/copy_libs.sh" ios
        else
            echo "警告: 找不到 copy_libs.sh 脚本"
        fi
    elif [[ -z "$NO_COPY_FLAG" ]]; then
        echo ""
        echo "提示: 使用 './copy_libs.sh ios' 复制库文件到项目目录"
        echo "     或运行构建脚本时添加 --copy 参数自动复制"
    fi
fi

