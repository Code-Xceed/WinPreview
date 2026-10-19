@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set CLANG_DIR=C:\Users\CodeX\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260908-ucrt-x86_64\bin
set PATH=%CLANG_DIR%;%PATH%

echo [WinPreview Native] Compiling resources...
"%CLANG_DIR%\llvm-windres.exe" resources.rc -O coff -o resources.res
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Resource compilation failed.
    exit /b %ERRORLEVEL%
)

echo [WinPreview Native] Compiling C++20 engine with Clang -O3 -flto -static -municode...
"%CLANG_DIR%\clang++.exe" -std=c++20 -O3 -flto -fno-exceptions -fno-rtti -static -mwindows -municode -s ^
    -DUNICODE -D_UNICODE -DNDEBUG -DNTDDI_VERSION=0x0A000005 ^
    main.cpp app_window.cpp d2d_engine.cpp wic_decoder.cpp pdf_engine.cpp media_engine.cpp text_engine.cpp explorer_hook.cpp svg_engine.cpp font_engine.cpp folder_engine.cpp markdown_engine.cpp system_engine.cpp resources.res ^
    -o WinPreview.exe ^
    -ld2d1 -ldwrite -lwindowscodecs -ld3d11 -ldwmapi -lole32 -loleaut32 -lshell32 -lshcore -lshlwapi -luxtheme -lcomdlg32 -lpropsys -luuid -lversion -lwintrust -lcrypt32

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed.
    exit /b %ERRORLEVEL%
)

echo [WinPreview Native] Build SUCCESS! Output: WinPreview.exe
dir WinPreview.exe
