#!/bin/bash

# 构建完成后复制库文件到 Android 和 iOS 项目目录
# 使用方法: ./copy_libs.sh [android|ios|all]
# 默认: all

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 项目根目录：复制到 go_gba（mgba-master 的同级目录）
# 路径：libretro-build -> mgba-master -> gba 根目录 -> go_gba
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../go_gba" && pwd)"

# 构建输出目录
BUILD_DIR="$SCRIPT_DIR/libs"

# Android 目标目录
ANDROID_TARGET_DIR="$PROJECT_ROOT/android/app/src/main/jniLibs/arm64-v8a"
ANDROID_SOURCE="$BUILD_DIR/arm64-v8a/libretro.so"

# iOS 目标目录
IOS_XCFRAMEWORK_DIR="$PROJECT_ROOT/ios/Runner/libretro.xcframework"
IOS_DEVICE_SOURCE="$BUILD_DIR/ios-arm64/libretro.dylib"
IOS_SIM_ARM64_SOURCE="$BUILD_DIR/ios-simulator-arm64/libretro.dylib"

# 复制函数
copy_file() {
    local source=$1
    local target=$2
    local description=$3
    
    if [ ! -f "$source" ]; then
        echo -e "${RED}❌ 错误: 源文件不存在: $source${NC}"
        echo -e "${YELLOW}   提示: 请先运行构建脚本${NC}"
        return 1
    fi
    
    # 创建目标目录
    local target_dir=$(dirname "$target")
    mkdir -p "$target_dir"
    
    # 复制文件
    cp "$source" "$target"
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓${NC} $description"
        echo -e "   ${BLUE}从:${NC} $source"
        echo -e "   ${BLUE}到:${NC} $target"
        return 0
    else
        echo -e "${RED}✗${NC} $description 复制失败"
        return 1
    fi
}

# 复制 Android 库
copy_android() {
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}复制 Android 库文件${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo ""
    
    copy_file "$ANDROID_SOURCE" \
              "$ANDROID_TARGET_DIR/libretro.so" \
              "Android ARM64 库"
    
    echo ""
}

# 复制 iOS 库
copy_ios() {
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}复制 iOS 库文件${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo ""
    
    local success_count=0
    local total_count=0
    
    # 复制真机版本
    if [ -f "$IOS_DEVICE_SOURCE" ]; then
        total_count=$((total_count + 1))
        if copy_file "$IOS_DEVICE_SOURCE" \
                     "$IOS_XCFRAMEWORK_DIR/ios-arm64/libretro.framework/libretro" \
                     "iOS 真机 (arm64) 库"; then
            success_count=$((success_count + 1))
        fi
        echo ""
    else
        echo -e "${YELLOW}⚠️  跳过: iOS 真机库不存在${NC}"
        echo ""
    fi
    
    # 复制模拟器 arm64 版本
    if [ -f "$IOS_SIM_ARM64_SOURCE" ]; then
        total_count=$((total_count + 1))
        if copy_file "$IOS_SIM_ARM64_SOURCE" \
                     "$IOS_XCFRAMEWORK_DIR/ios-simulator-arm64/libretro.framework/libretro" \
                     "iOS 模拟器 (arm64) 库"; then
            success_count=$((success_count + 1))
        fi
        echo ""
    else
        echo -e "${YELLOW}⚠️  跳过: iOS 模拟器 arm64 库不存在${NC}"
        echo ""
    fi
    
    if [ $total_count -eq 0 ]; then
        echo -e "${RED}❌ 错误: 没有找到任何 iOS 库文件${NC}"
        echo -e "${YELLOW}   提示: 请先运行 ./build_ios.sh${NC}"
        return 1
    fi
    
    if [ $success_count -eq $total_count ]; then
        return 0
    else
        return 1
    fi
}

# 主函数
main() {
    local target="${1:-all}"
    
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}复制构建库文件到项目目录${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    
    case "$target" in
        android)
            copy_android
            ;;
        ios)
            copy_ios
            ;;
        all)
            copy_android
            copy_ios
            ;;
        *)
            echo -e "${RED}错误: 未知的目标 '$target'${NC}"
            echo ""
            echo "用法: $0 [android|ios|all]"
            echo ""
            echo "参数:"
            echo "  android  - 只复制 Android 库"
            echo "  ios      - 只复制 iOS 库"
            echo "  all      - 复制所有库（默认）"
            exit 1
            ;;
    esac
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}复制完成！${NC}"
    echo -e "${GREEN}========================================${NC}"
}

# 运行主函数
main "$@"
