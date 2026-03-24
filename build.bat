@echo off
:: Windows 构建脚本 (MSVC + CMake) - 零依赖纯C++实现
setlocal

set CMAKE_EXE=
:: 尝试从 VS2022 找 cmake
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
)

:: 尝试系统 PATH 中的 cmake
if "%CMAKE_EXE%"=="" (
    where cmake >nul 2>&1
    if not errorlevel 1 set CMAKE_EXE=cmake
)

if "%CMAKE_EXE%"=="" (
    echo [错误] 未找到 CMake，请安装 Visual Studio 2022 或手动安装 CMake
    exit /b 1
)

echo [信息] 使用 CMake: %CMAKE_EXE%

"%CMAKE_EXE%" -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 ( echo [错误] CMake 配置失败 & exit /b 1 )

"%CMAKE_EXE%" --build build --config Release
if errorlevel 1 ( echo [错误] 编译失败 & exit /b 1 )

copy /Y build\Release\rtf2html.exe rtf2html.exe >nul
echo [成功] rtf2html.exe 已生成 - 零依赖，无需安装 LibreOffice
echo [用法] rtf2html.exe input.rtf output.html
endlocal
