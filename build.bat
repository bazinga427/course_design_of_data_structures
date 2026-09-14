@echo off
cd /d "%~dp0"
echo Compiling main.cpp + graph.cpp ...
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I third_party/easyx/include -L third_party/easyx/lib64 main.cpp graph.cpp -o main.exe -leasyx
if errorlevel 1 (
    echo.
    echo Build FAILED. Check that g++ from MinGW is in your system PATH.
    echo.
    pause
    exit /b 1
)
echo.
echo Build OK -^> main.exe
echo Input format: first number = start node, the rest = the sequence to check.
echo Example: 1 1 2 3 4
echo.
pause
