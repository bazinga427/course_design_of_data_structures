@echo off
setlocal
cd /d "%~dp0\.."

rem ---------------------------------------------------------------
rem Build the C++ core into public\wasm\graph_core.js (+ graph_core.wasm)
rem KEEP THIS FILE PURE ASCII WITH CRLF LINE ENDINGS (see build.bat).
rem ---------------------------------------------------------------

rem If emsdk is installed but not on PATH, point EMSDK at it here:
rem set "EMSDK=D:\soft\emsdk"

if defined EMSDK if exist "%EMSDK%\emsdk_env.bat" call "%EMSDK%\emsdk_env.bat" >nul 2>nul

where emcc >nul 2>nul
if errorlevel 1 (
    echo [x] Cannot find emcc.
    echo.
    echo     Install the Emscripten SDK once:
    echo       git clone https://github.com/emscripten-core/emsdk
    echo       cd emsdk
    echo       emsdk install latest
    echo       emsdk activate latest
    echo       emsdk_env.bat
    echo.
    echo     Then run this script again (or set EMSDK at the top of this file).
    echo     The web page still works without WASM: src/wasm/loader.js falls
    echo     back to a JavaScript copy of the same algorithms.
    exit /b 1
)

rem Sources: prefer cpp\ graph.cpp / flow.cpp / graph_io.cpp, otherwise the repo root.
set "SRC=cpp"
if not exist "%SRC%\graph.cpp" set "SRC=."
if not exist "%SRC%\graph.cpp" (
    echo [x] graph.cpp not found in cpp\ or in the project root.
    exit /b 1
)

rem binding.cpp is the web adapter: normally in the project root, but it also
rem works when it sits next to the C++ sources in cpp\.
set "BIND=%SRC%\binding.cpp"
if not exist "%BIND%" set "BIND=binding.cpp"
if not exist "%BIND%" (
    echo [x] binding.cpp not found in %SRC%\ or in the project root.
    exit /b 1
)

if not exist "public\wasm" mkdir "public\wasm"

echo Building the WASM core ...
call emcc --version | findstr /r /c:"emcc"
echo Sources : %BIND% %SRC%\graph.cpp %SRC%\flow.cpp %SRC%\graph_io.cpp
echo Output  : public\wasm\graph_core.js
echo.

call emcc -O2 -std=c++17 -I "%SRC%" --no-entry ^
    "%BIND%" "%SRC%\graph.cpp" "%SRC%\flow.cpp" "%SRC%\graph_io.cpp" ^
    -o public\wasm\graph_core.js ^
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createGraphCore ^
    -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 ^
    -sFORCE_FILESYSTEM=1 -sASSERTIONS=0 ^
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,stringToUTF8 ^
    -sEXPORTED_FUNCTIONS=_gv_version,_gv_load_text,_gv_random,_gv_save_text,_gv_snapshot,_gv_traverse,_gv_check_sequence,_gv_max_flow

if errorlevel 1 (
    echo.
    echo [x] Build FAILED. Read the compiler output above.
    echo     If it complains about the source encoding of graph.cpp / flow.cpp,
    echo     re-save those files as UTF-8 (they are GBK today) and build again.
    exit /b 1
)

echo.
echo [ok] public\wasm\graph_core.js is ready. Reload the web page (F5).
endlocal
