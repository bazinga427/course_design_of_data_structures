@echo off
setlocal
cd /d "%~dp0"

rem ---------------------------------------------------------------
rem Build the whole project -> main.exe
rem KEEP THIS FILE PURE ASCII WITH CRLF LINE ENDINGS.
rem cmd.exe reads .bat files using the OEM code page (936/GBK here).
rem UTF-8 Chinese comments break line and command boundaries there,
rem so rem-lines get executed and the window closes on an error.
rem ---------------------------------------------------------------

rem Prefer the compiler you installed; otherwise take the one in PATH.
set "MINGW_GXX=D:\soft\mingw64\bin\g++.exe"
if not exist "%MINGW_GXX%" set "MINGW_GXX="

if not defined MINGW_GXX for /f "delims=" %%i in ('where g++ 2^>nul') do if not defined MINGW_GXX set "MINGW_GXX=%%i"

if not defined MINGW_GXX (
    echo Cannot find g++.
    echo Install 64-bit MinGW-w64 and add its bin folder to PATH,
    echo or fix MINGW_GXX at the top of this file.
    pause
    exit /b 1
)

"%MINGW_GXX%" --version >nul 2>nul
if errorlevel 1 (
    echo Cannot run: %MINGW_GXX%
    pause
    exit /b 1
)

echo Compiler: %MINGW_GXX%
echo Compiling main.cpp + graph.cpp + flow.cpp + ui.cpp + layout.cpp + graph_io.cpp ...
echo.

"%MINGW_GXX%" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I third_party\easyx\include -L third_party\easyx\lib64 main.cpp graph.cpp flow.cpp ui.cpp layout.cpp graph_io.cpp -o main.exe -leasyx -lcomdlg32

if errorlevel 1 (
    echo.
    echo Build FAILED. The reason is in the compiler output above.
    pause
    exit /b 1
)

echo.
echo Build OK -^> main.exe
echo   1 = console menu  (traversal / check sequence / max flow / min cut)
echo   2 = EasyX window   (drag / zoom / scroll / open and save graph files)
echo.

choice /c YN /n /m "Open the graphic window now? (Y=run / N=quit): "
if errorlevel 2 goto :end
start "" main.exe 1

:end
endlocal
