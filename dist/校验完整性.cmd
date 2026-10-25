@echo off
chcp 936 >nul
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

if not exist "%ROOT%\SHA256SUMS.txt" (
    echo 找不到 SHA256SUMS.txt，请在本包根目录运行本脚本。
    pause
    exit /b 2
)
if not exist "%ROOT%\校验完整性.ps1" (
    echo 找不到 校验完整性.ps1，请确认整个包是完整拷贝的。
    pause
    exit /b 2
)

echo 正在校验 %ROOT%\ 全部文件，约 800 MB，需要一两分钟，请耐心等待。
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\校验完整性.ps1" -Root "%ROOT%"
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo 校验失败：上面列出了与发布时不一致的文件。
    echo 如果是你自己改过包里的配置（如 .ini），属正常，可忽略。
) else (
    echo 全部校验通过，包完好。
)
echo.
pause
exit /b %RC%
