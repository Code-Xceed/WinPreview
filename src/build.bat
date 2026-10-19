@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

:: 1. Discover Clang / LLVM toolchain dynamically (local winget, standard paths, or CI PATH)
set "CLANG_DIR="

where clang++.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    for /f "delims=" %%I in ('where clang++.exe') do (
        set "CLANG_DIR=%%~dpI"
        goto :found_clang
    )
)

:: Search Windows WinGet packages directory dynamically across all users
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\*llvm-mingw*") do (
    for /d %%B in ("%%D\*") do (
        if exist "%%B\bin\clang++.exe" (
            set "CLANG_DIR=%%B\bin\"
            goto :found_clang
        )
    )
)

:: Search standard 64-bit Program Files locations
if exist "%ProgramFiles%\LLVM\bin\clang++.exe" (
    set "CLANG_DIR=%ProgramFiles%\LLVM\bin\"
    goto :found_clang
)
if exist "%ProgramFiles(x86)%\LLVM\bin\clang++.exe" (
    set "CLANG_DIR=%ProgramFiles(x86)%\LLVM\bin\"
    goto :found_clang
)

:found_clang
if defined CLANG_DIR (
    set "PATH=%CLANG_DIR%;%PATH%"
    set "CLANG_EXE=%CLANG_DIR%clang++.exe"
    set "WINDRES_EXE=%CLANG_DIR%llvm-windres.exe"
) else (
    set "CLANG_EXE=clang++.exe"
    set "WINDRES_EXE=llvm-windres.exe"
)

echo [WinPreview Native] Toolchain: !CLANG_EXE!
echo [WinPreview Native] Compiling resources...
"!WINDRES_EXE!" resources.rc -O coff -o resources.res
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Resource compilation failed.
    exit /b %ERRORLEVEL%
)

echo [WinPreview Native] Compiling C++20 engine with Clang -O3 -flto -static -municode...
"!CLANG_EXE!" -std=c++20 -O3 -flto -fno-exceptions -fno-rtti -static -mwindows -municode -s ^
    -DUNICODE -D_UNICODE -DNDEBUG -DNTDDI_VERSION=0x0A000005 ^
    main.cpp app_window.cpp d2d_engine.cpp wic_decoder.cpp pdf_engine.cpp media_engine.cpp text_engine.cpp explorer_hook.cpp svg_engine.cpp font_engine.cpp folder_engine.cpp markdown_engine.cpp system_engine.cpp update_engine.cpp resources.res ^
    -o WinPreview.exe ^
    -ld2d1 -ldwrite -lwindowscodecs -ld3d11 -ldwmapi -lole32 -loleaut32 -lshell32 -lshcore -lshlwapi -luxtheme -lcomdlg32 -lpropsys -luuid -lversion -lwintrust -lcrypt32 -lwininet

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed.
    exit /b %ERRORLEVEL%
)

echo [WinPreview Native] Build SUCCESS! Output: WinPreview.exe
dir WinPreview.exe
