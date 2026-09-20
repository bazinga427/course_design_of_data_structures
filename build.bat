@echo off
rem 编译整个项目，生成 main.exe。直接双击本文件即可。
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo Cannot find g++ in PATH.
    echo Install 64-bit MinGW-w64 and add its bin folder to PATH, then run this again.
    pause
    exit /b 1
)

echo Compiling main.cpp + graph.cpp + flow.cpp ...

rem -I / -L / -leasyx 这三项是给 EasyX 准备的，以后加 ui.cpp 画图时直接用，
rem 现在的 main.cpp 还没有 include EasyX，链接它不会有任何副作用。
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I third_party/easyx/include -L third_party/easyx/lib64 main.cpp graph.cpp flow.cpp -o main.exe -leasyx

if errorlevel 1 (
    echo.
    echo Build FAILED. The reason is in the compiler output above.
    pause
    exit /b 1
)

echo.
echo Build OK -^> main.exe
pause
