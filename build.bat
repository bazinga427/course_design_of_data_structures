@echo off
rem 编译整个项目，生成 main.exe。直接双击本文件即可。
setlocal
cd /d "%~dp0"

rem 优先用这台机器上装好的那份 MinGW；找不到就退回 PATH 里的 g++，
rem 换电脑时改这一行即可。不用直接写 g++ 是因为 PATH 里可能先撞上
rem Dev-Cpp / MSYS2 的旧编译器，链接 EasyX 时会报一堆看不懂的错。
set "MINGW_GXX=D:\soft\mingw64\bin\g++.exe"
if not exist "%MINGW_GXX%" set "MINGW_GXX=g++"

"%MINGW_GXX%" --version >nul 2>nul
if errorlevel 1 (
    echo Cannot find a working g++.
    echo Install 64-bit MinGW-w64 and add its bin folder to PATH, or fix MINGW_GXX in this file.
    pause
    exit /b 1
)

rem UCRT 版的 MinGW 链接 libeasyx.a 时缺 __imp___iob_func，需要这个补丁文件；
rem msvcrt 版（比如 TDM-GCC）不需要它。文件不在就跳过，不影响编译。
set "COMPAT="
if exist "tools\easyx_ucrt_compat.c" set "COMPAT=tools/easyx_ucrt_compat.c"

echo Compiler: %MINGW_GXX%
echo Compiling main.cpp + graph.cpp + flow.cpp + ui.cpp + layout.cpp + graph_io.cpp ...

g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I third_party/easyx/include -L third_party/easyx/lib64 main.cpp graph.cpp flow.cpp ui.cpp layout.cpp graph_io.cpp %COMPAT% -o main.exe -leasyx -lcomdlg32

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
