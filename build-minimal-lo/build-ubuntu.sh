#!/bin/bash
# 最小化 LOKit 构建脚本 - Ubuntu 22.04/24.04 (WSL2 或原生 Linux)
# 用法：bash build-ubuntu.sh [源码目录] [输出目录]
# 示例：bash build-ubuntu.sh /mnt/c/Users/20274/Documents/core-master /opt/lo-minimal

set -euo pipefail

LO_SRC="${1:-/mnt/c/Users/20274/Documents/core-master}"
OUTPUT_DIR="${2:-$HOME/lo-minimal-output}"
BUILD_DIR="$HOME/lo-minimal-build"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
JOBS=$(nproc)

echo "=== LibreOffice 最小化 LOKit 构建 ==="
echo "  源码：$LO_SRC"
echo "  构建：$BUILD_DIR"
echo "  输出：$OUTPUT_DIR"
echo "  并行：$JOBS 核心"
echo ""

# ---- 1. 安装 Ubuntu 构建依赖 ----
install_deps() {
    echo "[1/5] 安装构建依赖..."
    sudo apt-get update -qq
    sudo apt-get install -y --no-install-recommends \
        build-essential \
        autoconf automake libtool pkg-config \
        bison flex gperf \
        git wget curl \
        nasm yasm \
        python3 python3-dev \
        libx11-dev libxext-dev libxrender-dev libxrandr-dev libxinerama-dev \
        libfontconfig1-dev libfreetype6-dev \
        libpng-dev libjpeg-dev \
        libxml2-dev libxslt1-dev \
        libssl-dev \
        zlib1g-dev \
        libcurl4-openssl-dev \
        libglib2.0-dev \
        libicudev-data libicu-dev \
        openjdk-11-jdk-headless \
        ant \
        zip unzip \
        gettext \
        rsync \
        ucpp \
        libcups2-dev \
        libharfbuzz-dev \
        libgl1-mesa-dev \
        libglu1-mesa-dev \
        libnss3-dev \
        libasound2-dev
    echo "[1/5] 完成"
}

# ---- 2. 准备源码目录 ----
prepare_source() {
    echo "[2/5] 准备源码..."
    if [ ! -d "$LO_SRC/sw" ]; then
        echo "错误：$LO_SRC 不是有效的 LibreOffice 源码目录（缺少 sw/）"
        exit 1
    fi

    # 复制 autogen.input 到源码目录
    cp "$SCRIPT_DIR/autogen.input" "$LO_SRC/autogen.input"
    echo "[2/5] autogen.input 已复制"

    # 可选：应用最小化 mergelibs 补丁（减少 mergedlo.so 约 15-25%）
    # 如果要应用补丁，取消下面的注释：
    # (cd "$LO_SRC" && patch -p1 < "$SCRIPT_DIR/patch-mergelibs-minimal.diff") || true
}

# ---- 3. 配置 ----
configure_build() {
    echo "[3/5] 运行 autogen.sh 配置..."
    mkdir -p "$BUILD_DIR"
    cd "$LO_SRC"

    # 生成 configure 脚本（如果不存在）
    if [ ! -f "$LO_SRC/configure" ]; then
        ./autogen.sh
    else
        ./autogen.sh
    fi
    echo "[3/5] 配置完成"
}

# ---- 4. 构建 ----
build_lo() {
    echo "[4/5] 开始编译（预计 1-3 小时）..."
    cd "$LO_SRC"

    # 只构建需要的模块：
    #   sal, cppu, cppuhelper - UNO 基础
    #   tools, comphelper, sax - 工具库
    #   i18npool, i18nutil - 国际化
    #   editeng, svl, svt - 编辑引擎
    #   vcl - 渲染（headless 模式）
    #   sw - Writer 核心
    #   sfx2 - 文档框架
    #   oox, msfilter - 文件格式
    # 如果只构建部分模块，使用：make sw
    # 但 LOKit 需要 soffice 链接完整，所以用：make build-nocheck

    make -j"$JOBS" build-nocheck 2>&1 | tee "$HOME/lo-build.log"
    echo "[4/5] 编译完成"
}

# ---- 5. 打包输出 ----
package_output() {
    echo "[5/5] 打包 LOKit 库文件..."
    mkdir -p "$OUTPUT_DIR/lib"
    mkdir -p "$OUTPUT_DIR/program"
    mkdir -p "$OUTPUT_DIR/include"

    # 找到构建输出目录（instdir 或 workdir）
    INSTDIR="$LO_SRC/instdir"
    if [ ! -d "$INSTDIR" ]; then
        echo "警告：未找到 instdir，尝试 workdir..."
        INSTDIR="$LO_SRC/workdir"
    fi

    # 复制 LOKit 核心库
    find "$LO_SRC" -name "libmergedlo.so" -o -name "libsw*.so" | while read f; do
        cp "$f" "$OUTPUT_DIR/lib/" && echo "  已复制: $(basename $f)"
    done

    # 复制 LOKit 头文件
    cp -r "$LO_SRC/include/LibreOfficeKit" "$OUTPUT_DIR/include/" 2>/dev/null || true

    # 复制 program 目录
    if [ -d "$INSTDIR/program" ]; then
        rsync -av --include="*.so" --include="*.so.*" --exclude="*" \
              "$INSTDIR/program/" "$OUTPUT_DIR/program/" 2>/dev/null || true
        cp -r "$INSTDIR/program" "$OUTPUT_DIR/" 2>/dev/null || true
    fi

    echo ""
    echo "=== 输出文件大小 ==="
    du -sh "$OUTPUT_DIR"/*/ 2>/dev/null || true
    find "$OUTPUT_DIR" -name "*.so" -exec du -sh {} \; | sort -h | tail -20

    echo ""
    echo "[5/5] 打包完成：$OUTPUT_DIR"
}

# ---- 主流程 ----
case "${3:-all}" in
    deps)     install_deps ;;
    prepare)  prepare_source ;;
    config)   configure_build ;;
    build)    build_lo ;;
    package)  package_output ;;
    all)
        install_deps
        prepare_source
        configure_build
        build_lo
        package_output
        ;;
    *)
        echo "用法：$0 [源码目录] [输出目录] [all|deps|prepare|config|build|package]"
        exit 1
        ;;
esac
