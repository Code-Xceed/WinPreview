@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building WinPreview Native Release Engine
echo =======================================================

cd /d "%~dp0src"
call build.bat
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Native build failed!
    exit /b %ERRORLEVEL%
)

cd /d "%~dp0"
if not exist "dist\WinPreview" mkdir "dist\WinPreview"
copy /y "src\WinPreview.exe" "dist\WinPreview\WinPreview.exe" >nul
copy /y "src\pdfium.dll" "dist\WinPreview\pdfium.dll" >nul

echo.
echo [SUCCESS] WinPreview packaged in dist\WinPreview:
dir "dist\WinPreview"
