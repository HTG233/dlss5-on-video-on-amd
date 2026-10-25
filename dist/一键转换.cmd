@echo off
setlocal enabledelayedexpansion
title HTG DLSS5-AMD 神经渲染转换
if "%~1"=="" goto usage

set "EXE=%~dp0engine\dlss5-convert.exe"
set "FAILED=0"

:loop
if "%~1"=="" goto done
set "SRC=%~1"

rem 目录整体转换：逐个视频处理，输出文件名加 -nr，容器用 MKV
if exist "%SRC%\" (
    echo.
    echo   目录: %SRC%
    echo   说明: 每个视频各转一遍，文件名加 -nr，输出 MKV
    echo.
    cd /d "%~dp0engine"
    "%EXE%" "%SRC%" --stages nr --suffix -nr --overwrite
    cd /d "%~dp0"
    set "RC=!ERRORLEVEL!"
    if not "!RC!"=="0" (
        echo   失败，退出码 !RC! - 详细日志在 engine\DLSSVideoPlayer.log
        set "FAILED=1"
    ) else (
        echo   完成
    )
    shift
    goto loop
)

rem 单文件：跟随源容器，mkv/webm 出 mkv，其余出 mp4
set "EXT=mp4"
if /i "%~x1"==".mkv" set "EXT=mkv"
if /i "%~x1"==".webm" set "EXT=mkv"
set "OUT=%~dpn1-nr.%EXT%"

echo.
echo   源文件: %SRC%
echo   输出  : %OUT%
echo.

cd /d "%~dp0engine"
"%EXE%" "%SRC%" --stages nr --out "%OUT%" --overwrite
cd /d "%~dp0"
set "RC=!ERRORLEVEL!"
if not "!RC!"=="0" (
    echo   失败，退出码 !RC! - 详细日志在 engine\DLSSVideoPlayer.log
    set "FAILED=1"
) else (
    echo   完成
)

shift
goto loop

:done
echo.
if "%FAILED%"=="1" (echo   有文件失败。) else (echo   全部完成。)
pause
exit /b %FAILED%

:usage
echo.
echo   HTG DLSS5-AMD v1.01
echo.
echo   把一个或多个视频、文件夹拖到本 .cmd 图标上即可。
echo.
echo   输出格式: MKV / WEBM 源出 MKV，其余出 MP4，文件名加 -nr
echo   同名文件已存在时直接覆盖（--overwrite）
echo.
echo   图形界面: 双击 HTG-DLSS5-AMD.exe
echo.
echo   可选环境变量:
echo     AMDNR_PASSES=1^|2^|3     网络过几遍，默认 1
echo     AMDLSS_NR_ASSETS=路径    自定义运行时资产目录
echo.
pause
exit /b 2
