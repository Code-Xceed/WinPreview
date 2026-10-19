@echo off
setlocal
cd /d "%~dp0"

if exist "dist\WinPreview\WinPreview.exe" (
    start "" "dist\WinPreview\WinPreview.exe" %*
) else if exist "src\WinPreview.exe" (
    start "" "src\WinPreview.exe" %*
) else (
    echo Building native WinPreview...
    call build.bat
    if exist "dist\WinPreview\WinPreview.exe" (
        start "" "dist\WinPreview\WinPreview.exe" %*
    )
)
