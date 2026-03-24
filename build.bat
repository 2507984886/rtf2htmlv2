@echo off
:: Windows 构建脚本（MSVC + CMake）
setlocal

set CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

set LO_DIR=
if exist "C:\Program Files\LibreOffice" set LO_DIR=C:\Program Files\LibreOffice
if exist "C:\Program Files (x86)\LibreOffice" set LO_DIR=C:\Program Files (x86)\LibreOffice

if "%LO_DIR%"=="" (
    echo [错误] 未找到 LibreOffice，请先安装: winget install TheDocumentFoundation.LibreOffice
    exit /b 1
)

echo [信息] LibreOffice: %LO_DIR%

"%CMAKE_EXE%" -B build -G "Visual Studio 17 2022" -A x64 -DLIBREOFFICE_DIR="%LO_DIR%"
if errorlevel 1 ( echo [错误] CMake 配置失败 & exit /b 1 )

"%CMAKE_EXE%" --build build --config Release
if errorlevel 1 ( echo [错误] 编译失败 & exit /b 1 )

copy /Y build\Release\rtf2html.exe rtf2html.exe >nul
echo [成功] build\Release\rtf2html.exe
endlocal
