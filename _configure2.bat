@echo off
setlocal
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "MSVC=%VSDIR%\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

set "PATH=%MSVC%\bin\Hostx64\x64;%SDK%\bin\%SDKVER%\x64;C:\Windows\System32;C:\Windows"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\winrt"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

"%CMAKE%" -S "%~dp0." -B "%~dp0build-amd" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_C_COMPILER="%MSVC%\bin\Hostx64\x64\cl.exe" -DCMAKE_CXX_COMPILER="%MSVC%\bin\Hostx64\x64\cl.exe" -DCMAKE_RC_COMPILER="C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" -DDLSS_SDK="%~dp0external\DLSS" -DBUILD_TESTING=OFF
exit /b %errorlevel%
