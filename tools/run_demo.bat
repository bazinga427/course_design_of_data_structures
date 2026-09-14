@echo off
cd /d "%~dp0"
echo ===== EasyX demo build =====
echo.
echo [1/3] Checking the compiler ...
where g++
if errorlevel 1 (
    echo.
    echo Cannot find g++ in PATH. Install MinGW and add its bin folder to PATH.
    echo.
    pause
    exit /b 1
)
g++ -dumpmachine
echo.
echo [2/3] Checking EasyX files ...
if not exist "..\third_party\easyx\include\graphics.h" (
    echo.
    echo Missing ..\third_party\easyx\include\graphics.h
    echo Your copy of the project is out of date. Run this first:  git pull
    echo.
    pause
    exit /b 1
)
echo EasyX files found.
echo.
echo [3/3] Compiling ...
g++ -std=c++17 -I "..\third_party\easyx\include" -L "..\third_party\easyx\lib64" easyx_demo.cpp -o easyx_demo.exe -leasyx
if errorlevel 1 (
    echo.
    echo ---------------------------------------------
    echo Build FAILED. The real reason is in the compiler output above.
    echo Please send a screenshot of this whole window.
    echo ---------------------------------------------
    echo.
    pause
    exit /b 1
)
echo Build OK, opening the window ...
start "" easyx_demo.exe
