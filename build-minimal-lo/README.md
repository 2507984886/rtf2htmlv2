# 最小化 LibreOffice LOKit 源码构建方案

## 目标

从 LibreOffice 源码编译一个**最小化的 LOKit 库**，仅包含：
- Writer（文字处理器核心）
- RTF 读取过滤器
- HTML 导出过滤器

与完整 LibreOffice 完全相同的输出（使用相同的转换引擎），但体积大幅缩减。

## 预期大小对比

| 方案 | 大小 |
|------|------|
| 完整 LibreOffice（当前） | ~386 MB |
| 最小化 LOKit（源码编译） | ~120-150 MB |
| 纯 C++ 重实现（已弃用） | 63 KB（但输出有差异） |

> **注意**：即使是最小化构建，LOKit 仍需约 120-150 MB，因为 ICU 数据（~32 MB）、
> mergedlo 核心库（~80-100 MB）、VCL headless 渲染引擎等都不可缺少。

## 构建环境要求

### Windows（使用 WSL2 构建 Linux 版本）
1. 安装 WSL2 Ubuntu（以管理员身份运行 PowerShell）：
   ```powershell
   wsl --install -d Ubuntu-24.04
   ```
2. 重启电脑，完成 Ubuntu 初始设置
3. 在 WSL Ubuntu 中运行构建脚本

### macOS
直接在 macOS 终端运行，需要 Xcode Command Line Tools。

### HarmonyOS / Android
需要 NDK 交叉编译工具链（TODO）。

## 快速开始（Windows WSL2）

```bash
# 在 WSL Ubuntu 终端中执行：

# 1. 进入构建目录
cd /mnt/c/Users/20274/Documents/rtf2html/build-minimal-lo

# 2. 完整构建（约 1-3 小时）
bash build-ubuntu.sh \
  /mnt/c/Users/20274/Documents/core-master \
  ~/lo-minimal-output

# 或分步执行：
bash build-ubuntu.sh /mnt/c/Users/20274/Documents/core-master ~/lo-minimal-output deps
bash build-ubuntu.sh /mnt/c/Users/20274/Documents/core-master ~/lo-minimal-output prepare
bash build-ubuntu.sh /mnt/c/Users/20274/Documents/core-master ~/lo-minimal-output config
bash build-ubuntu.sh /mnt/c/Users/20274/Documents/core-master ~/lo-minimal-output build
bash build-ubuntu.sh /mnt/c/Users/20274/Documents/core-master ~/lo-minimal-output package
```

## 进一步减小体积（可选）

### 1. 应用 mergelibs 补丁（减少 ~20-30 MB）

取消 `build-ubuntu.sh` 中的注释行来应用补丁，移除合并库中的：
- `chart2`、`chart2api` — 图表渲染
- `slideshow`、`animcore` — 动画
- `rpt`、`rptui` — 报表构建器
- `solver` — Calc 线性规划求解器
- `analysis`、`date`、`pricing` — Calc 函数
- `for`、`forui`、`frm` — 表单控件
- `bib` — 参考文献

### 2. ICU 数据子集化（减少 ~20-25 MB）

使用 ICU 的 `icupkg` 工具创建仅包含所需 locale 的子集：
```bash
# 仅保留 en-US、zh-CN、zh-TW 所需的 ICU 数据
# （详见 https://unicode-org.github.io/icu/userguide/icu_data/buildtool.html）
```

### 3. UPX 压缩（减少 ~30-50%）
```bash
upx --best libmergedlo.so libswlo.so
```
> 注意：UPX 压缩会增加加载时间

## 运行时部署

构建完成后，`lo-minimal-output/program/` 目录包含所有运行时文件。
将该目录与 `rtf2html` 可执行文件一起分发：

```
rtf2html               ← 主程序（由 rtf2html-lokit.cpp 编译）
lo-minimal/
  program/
    libmergedlo.so      ← LOKit 核心（~80-100 MB）
    libswlo.so          ← Writer（~20 MB）
    libsw_writerfilterlo.so  ← RTF 过滤器（~4 MB）
    libicudt78l.so      ← ICU 数据（~32 MB，可子集化至 ~8 MB）
    ...
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `autogen.input` | LibreOffice 最小化构建的 configure 参数 |
| `build-ubuntu.sh` | Ubuntu/WSL2 一键构建脚本 |
| `patch-mergelibs-minimal.diff` | 移除非必要库的补丁（可选） |
| `rtf2html-lokit.cpp` | 使用 LOKit API 的 rtf2html 转换程序 |
