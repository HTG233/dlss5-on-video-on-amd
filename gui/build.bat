@echo off
setlocal
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "MSVC=%VSDIR%\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"

set "PATH=%MSVC%\bin\Hostx64\x64;%SDK%\bin\%SDKVER%\x64;C:\Windows\System32;C:\Windows"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\winrt"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

cl /nologo /utf-8 /O2 /W3 /EHsc /Fe:HTG-DLSS5-AMD.exe htg_gui.cpp /link /SUBSYSTEM:WINDOWS /MACHINE:X64 user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib dwmapi.lib
exit /b %ERRORLEVEL%
