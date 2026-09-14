@echo off
cd /d "%~dp0"
echo Compiling EasyX demo ...
g++ -std=c++17 -I "..\third_party\easyx\include" -L "..\third_party\easyx\lib64" easyx_demo.cpp -o easyx_demo.exe -leasyx
if errorlevel 1 (
    echo.
    echo Build FAILED. Check that g++ from MinGW is in your system PATH.
    echo.
    pause
    exit /b 1
)
echo Build OK, opening the window ...
start "" easyx_demo.exe
