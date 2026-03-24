#!/bin/bash
# LibreOffice 最小化 LOKit Windows 构建脚本
# 在 Cygwin 终端中运行（不是 Git Bash）
#
# 用法：bash build-windows.sh [源码目录] [输出目录]
# 示例：bash build-windows.sh /cygdrive/c/Users/20274/Documents/core-master /cygdrive/c/lo-output

set -euo pipefail

LO_SRC="${1:-/cygdrive/c/Users/20274/Documents/core-master}"
OUTPUT_DIR="${2:-/cygdrive/c/lo-minimal-win}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
JOBS=$(nproc 2>/dev/null || echo 4)

echo "=== LibreOffice 最小化 LOKit Windows 构建 ==="
echo "  源码：$LO_SRC"
echo "  输出：$OUTPUT_DIR"
echo "  并行：$JOBS 核心"

# ---- 检查 Cygwin 环境 ----
check_env() {
    echo "[检查] 验证环境..."
    command -v make   || { echo "错误：未找到 make，请确认 Cygwin 已安装"; exit 1; }
    command -v perl   || { echo "错误：未找到 perl"; exit 1; }
    command -v python3 || command -v python || { echo "警告：未找到 python3"; }

    # 查找 MSVC（cl.exe）
    CL=$(find "/cygdrive/c/Program Files/Microsoft Visual Studio/2022" \
         -name "cl.exe" -path "*/x64/*" 2>/dev/null | head -1)
    if [ -z "$CL" ]; then
        echo "错误：未找到 MSVC cl.exe，请确认 VS2022 已安装"
        exit 1
    fi
    echo "  MSVC：$CL"
}

# ---- 配置 ----
configure_build() {
    echo "[1/3] 配置..."
    cp "$SCRIPT_DIR/autogen-windows.input" "$LO_SRC/autogen.input"
    cd "$LO_SRC"
    ./autogen.sh
    echo "[1/3] 配置完成"
}

# ---- 编译 ----
build_lo() {
    echo "[2/3] 编译（预计 1-3 小时）..."
    cd "$LO_SRC"
    make -j"$JOBS" build-nocheck 2>&1 | tee "$HOME/lo-build-win.log"
    echo "[2/3] 编译完成"
}

# ---- 打包 ----
package_output() {
    echo "[3/3] 打包..."
    mkdir -p "$OUTPUT_DIR"
    INSTDIR="$LO_SRC/instdir"

    # 复制可执行文件和 DLL
    if [ -d "$INSTDIR/program" ]; then
        cp -r "$INSTDIR/program" "$OUTPUT_DIR/"
        # strip 调试符号
        find "$OUTPUT_DIR/program" -name "*.dll" -exec strip {} \; 2>/dev/null || true
    fi

    # 复制头文件
    cp -r "$LO_SRC/include/LibreOfficeKit" "$OUTPUT_DIR/" 2>/dev/null || true

    # 编译 rtf2html.exe（调用 LOKit）
    # 使用系统 cl.exe 编译
    cl.exe //std:c++17 //O2 //EHsc \
        "$SCRIPT_DIR/rtf2html-lokit.cpp" \
        //Fe:"$OUTPUT_DIR/rtf2html.exe" \
        kernel32.lib user32.lib shell32.lib 2>/dev/null || \
    g++ -std=c++17 -O2 -o "$OUTPUT_DIR/rtf2html.exe" \
        "$SCRIPT_DIR/rtf2html-lokit.cpp" -lkernel32 -luser32 -lshell32 2>/dev/null || \
    echo "警告：rtf2html.exe 编译失败，请手动用 VS 编译 rtf2html-lokit.cpp"

    echo ""
    echo "=== 输出文件大小 ==="
    du -sh "$OUTPUT_DIR"/* | sort -h

    echo ""
    echo "[3/3] 完成：$OUTPUT_DIR"
}

check_env
configure_build
build_lo
package_output
