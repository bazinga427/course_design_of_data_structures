@echo off
setlocal
cd /d "%~dp0"

rem Prefer the MinGW that is known to work on this machine (same one as .vscode/tasks.json).
rem If it is not there, fall back to the g++ on PATH so teammates on other machines can still build.
set "MINGW_GXX=D:\soft\mingw64\bin\g++.exe"
if not exist "%MINGW_GXX%" set "MINGW_GXX=g++"

echo Compiler: %MINGW_GXX%
"%MINGW_GXX%" --version | findstr /i "g++"
echo.
echo Compiling main.cpp + graph.cpp + ui.cpp + layout.cpp + graph_io.cpp ...
"%MINGW_GXX%" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I third_party/easyx/include -L third_party/easyx/lib64 main.cpp graph.cpp ui.cpp layout.cpp graph_io.cpp tools/easyx_ucrt_compat.c -o main.exe -leasyx -lcomdlg32

if errorlevel 1 (
    echo.
    echo Build FAILED - send the error above to your teammate.
    echo  1. The compiler printed above should be D:\soft\mingw64\bin\g++.exe.
    echo     If it is not, an old MinGW ^(Dev-Cpp / MSYS2 / TDM-GCC^) comes first in PATH.
    echo  2. This file must stay in the same folder as main.cpp.
    echo.
    pause
    exit /b 1
)

echo.
echo Build OK -^> main.exe
echo   1 = graphic window  ^(drag / zoom / scrollbars / open and save graph files^)
echo   2 = console flow    ^(first number = start node, rest = sequence to check, e.g. 1 1 2 3 4^)
echo.

choice /c YN /n /m "Open the graphic window now? (Y=run / N=quit): "
if errorlevel 2 goto :end
start "" main.exe 1

:end
endlocal
