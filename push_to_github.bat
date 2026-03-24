@echo off
:: 推送到 GitHub 的辅助脚本
:: GitHub 已不支持密码认证，需要 Personal Access Token (PAT)
::
:: 获取 PAT 步骤：
::   1. 登录 GitHub → Settings → Developer settings
::      → Personal access tokens → Tokens (classic) → Generate new token
::   2. 勾选 repo 权限 → 生成 → 复制 token（ghp_xxx...）
::   3. 运行本脚本并粘贴 token

setlocal

set /p PAT="请粘贴你的 GitHub Personal Access Token (ghp_...): "

if "%PAT%"=="" (
    echo [错误] Token 为空
    exit /b 1
)

cd /d "%~dp0"

git remote set-url origin https://2507984886:%PAT%@github.com/2507984886/rtf2htmlv2.git
git push -u origin master

if errorlevel 1 (
    echo [错误] 推送失败，请检查 Token 是否正确
) else (
    echo [成功] 已推送到 https://github.com/2507984886/rtf2htmlv2
)

:: 安全起见，推送后恢复不含token的URL
git remote set-url origin https://github.com/2507984886/rtf2htmlv2.git

endlocal
