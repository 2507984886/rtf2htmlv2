@echo off
:: rtf2html 启动脚本 - 零依赖纯C++实现
setlocal
set SCRIPT_DIR=%~dp0
set RTF2HTML_EXE=

if exist "%SCRIPT_DIR%rtf2html.exe"              set RTF2HTML_EXE=%SCRIPT_DIR%rtf2html.exe
if exist "%SCRIPT_DIR%build\Release\rtf2html.exe" set RTF2HTML_EXE=%SCRIPT_DIR%build\Release\rtf2html.exe

if "%RTF2HTML_EXE%"=="" (
    echo [错误] 未找到 rtf2html.exe，请先运行 build.bat
    exit /b 1
)

"%RTF2HTML_EXE%" %*
exit /b %ERRORLEVEL%
