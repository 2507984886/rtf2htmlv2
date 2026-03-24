#!/usr/bin/env bash
# macOS / Linux / 鸿蒙 HarmonyOS 构建脚本
set -e

# 自动检测 LibreOffice 目录
detect_lo_dir() {
    if [[ "$(uname)" == "Darwin" ]]; then
        echo "/Applications/LibreOffice.app/Contents"
    else
        for d in /usr/lib/libreoffice /usr/lib64/libreoffice /opt/libreoffice /system/lib/libreoffice; do
            [[ -d "$d" ]] && echo "$d" && return
        done
    fi
}

LO_DIR="${1:-$(detect_lo_dir)}"

if [[ -z "$LO_DIR" ]]; then
    echo "[错误] 未找到 LibreOffice。请安装或指定路径: $0 /path/to/libreoffice"
    exit 1
fi

echo "[信息] LibreOffice: $LO_DIR"

cmake -B build -DCMAKE_BUILD_TYPE=Release -DLIBREOFFICE_DIR="$LO_DIR"
cmake --build build -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

cp build/rtf2html ./rtf2html
echo "[成功] ./rtf2html"
