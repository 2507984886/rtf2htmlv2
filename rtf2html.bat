@echo off
:: Windows 启动脚本，自动设置 LO DLL 路径
setlocal
set SCRIPT_DIR=%~dp0
set RTF2HTML_EXE=
if exist "%SCRIPT_DIR%rtf2html.exe" set RTF2HTML_EXE=%SCRIPT_DIR%rtf2html.exe
if exist "%SCRIPT_DIR%build\Release\rtf2html.exe" set RTF2HTML_EXE=%SCRIPT_DIR%build\Release\rtf2html.exe
if "%RTF2HTML_EXE%"=="" ( echo [错误] 未找到 rtf2html.exe，请先运行 build.bat & exit /b 1 )

if exist "C:\Program Files\LibreOffice\program"       set PATH=C:\Program Files\LibreOffice\program;%PATH%
if exist "C:\Program Files (x86)\LibreOffice\program" set PATH=C:\Program Files (x86)\LibreOffice\program;%PATH%

"%RTF2HTML_EXE%" %*
exit /b %ERRORLEVEL%
