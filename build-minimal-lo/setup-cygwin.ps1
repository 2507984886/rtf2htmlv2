# 安装 Cygwin 及 LibreOffice 构建所需的全部包
# 以管理员身份在 PowerShell 中运行：
#   powershell -ExecutionPolicy Bypass -File setup-cygwin.ps1

$CygwinDir    = "C:\cygwin64"
$CygwinSetup  = "$env:TEMP\cygwin-setup.exe"
$CygwinMirror = "https://mirrors.tuna.tsinghua.edu.cn/cygwin/"  # 清华镜像，国内速度快

# LibreOffice 构建所需的 Cygwin 包列表
$Packages = @(
    "autoconf", "automake", "libtool",
    "make", "perl", "patch",
    "bison", "flex", "gperf",
    "zip", "unzip", "gettext",
    "pkg-config", "nasm", "yasm",
    "wget", "curl",
    "git",
    "python3",
    "gcc-core", "gcc-g++",   # 用于 build-side 工具，实际编译器仍用 MSVC
    "diffutils", "findutils",
    "coreutils", "binutils",
    "libxml2-devel",
    "ucpp"
) -join ","

Write-Host "=== 下载 Cygwin 安装程序 ===" -ForegroundColor Cyan
Invoke-WebRequest -Uri "https://cygwin.com/setup-x86_64.exe" -OutFile $CygwinSetup

Write-Host "=== 安装 Cygwin 到 $CygwinDir ===" -ForegroundColor Cyan
Start-Process -Wait -FilePath $CygwinSetup -ArgumentList @(
    "--quiet-mode",
    "--no-desktop",
    "--root", $CygwinDir,
    "--site", $CygwinMirror,
    "--packages", $Packages
)

Write-Host "=== Cygwin 安装完成 ===" -ForegroundColor Green
Write-Host "路径：$CygwinDir\bin\bash.exe"
Write-Host ""
Write-Host "接下来在 Cygwin 终端中运行：" -ForegroundColor Yellow
Write-Host "  bash /cygdrive/c/Users/20274/Documents/rtf2html/build-minimal-lo/build-windows.sh"
