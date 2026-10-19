<#
.SYNOPSIS
    Compiles, packages, and produces a professional, self-contained Windows installer for WinPreview (Native C++20).
#>
param(
    [string]$Configuration = "Release",
    [string]$OutputDir = "dist\WinPreview"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  WinPreview - Native C++20 Ultra-Fast Windows Publisher   " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Terminate running instances before compiling
Get-Process WinPreview -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 100

# Step 1: Compile Native Engine
Write-Host "`n[1/3] Compiling Native C++20 Direct2D/D3D11 Engine..." -ForegroundColor Yellow
$buildBat = Join-Path $ScriptDir "src\build.bat"
if (-not (Test-Path $buildBat)) {
    throw "Build script not found: $buildBat"
}

$proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$buildBat`"" -WorkingDirectory (Join-Path $ScriptDir "src") -NoNewWindow -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "Native compilation failed with exit code $($proc.ExitCode)"
}

$builtExe = Join-Path $ScriptDir "src\WinPreview.exe"
if (-not (Test-Path $builtExe)) {
    throw "Expected binary was not produced: $builtExe"
}

# Step 2: Package Release Directory
Write-Host "`n[2/3] Staging release artifacts into $OutputDir..." -ForegroundColor Yellow
$targetPublishDir = Join-Path $ScriptDir $OutputDir
if (-not (Test-Path $targetPublishDir)) {
    New-Item -ItemType Directory -Path $targetPublishDir -Force | Out-Null
}

Copy-Item -LiteralPath $builtExe -Destination (Join-Path $targetPublishDir "WinPreview.exe") -Force
$pdfium = Join-Path $ScriptDir "src\pdfium.dll"
if (Test-Path $pdfium) {
    Copy-Item -LiteralPath $pdfium -Destination (Join-Path $targetPublishDir "pdfium.dll") -Force
}

$installedDir = Join-Path $env:LOCALAPPDATA "Programs\WinPreview"
if (Test-Path $installedDir) {
    Copy-Item -LiteralPath $builtExe -Destination (Join-Path $installedDir "WinPreview.exe") -Force
    if (Test-Path $pdfium) {
        Copy-Item -LiteralPath $pdfium -Destination (Join-Path $installedDir "pdfium.dll") -Force
    }
    Write-Host "  Updated installed program at $installedDir" -ForegroundColor Green
}

$appIco = Join-Path $ScriptDir "src\app.ico"
if (Test-Path $appIco) {
    Copy-Item -LiteralPath $appIco -Destination (Join-Path $targetPublishDir "app.ico") -Force
    if (Test-Path $installedDir) {
        Copy-Item -LiteralPath $appIco -Destination (Join-Path $installedDir "app.ico") -Force
    }
}
$appLogo = Join-Path $ScriptDir "src\app_logo.png"
if (Test-Path $appLogo) {
    Copy-Item -LiteralPath $appLogo -Destination (Join-Path $targetPublishDir "app_logo.png") -Force
    if (Test-Path $installedDir) {
        Copy-Item -LiteralPath $appLogo -Destination (Join-Path $installedDir "app_logo.png") -Force
    }
}

$exeSizeMb = [math]::Round((Get-Item (Join-Path $targetPublishDir "WinPreview.exe")).Length / 1MB, 2)
Write-Host "  Staged WinPreview.exe ($exeSizeMb MB standalone native binary)" -ForegroundColor Green

# Step 3: Compile Inno Setup Installer
Write-Host "`n[3/3] Building standalone Windows Setup Installer (WinPreview-Setup-x64.exe)..." -ForegroundColor Yellow
$isccPaths = @(
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)

$iscc = $null
foreach ($p in $isccPaths) {
    if (Test-Path $p) {
        $iscc = $p
        break
    }
}
if (-not $iscc) {
    $cmd = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}

if ($iscc) {
    $issFile = Join-Path $ScriptDir "installer\WinPreview.iss"
    $isccProc = Start-Process -FilePath $iscc -ArgumentList "`"$issFile`"" -NoNewWindow -Wait -PassThru
    if ($isccProc.ExitCode -ne 0) {
        throw "Inno Setup compilation failed with exit code $($isccProc.ExitCode)"
    }
    
    $installerPath = Join-Path $ScriptDir "dist\Installer\WinPreview-Setup-x64.exe"
    if (Test-Path $installerPath) {
        $installerSizeMb = [math]::Round((Get-Item $installerPath).Length / 1MB, 2)
        Write-Host "`n[SUCCESS] Standalone Windows Installer created successfully!" -ForegroundColor Green
        Write-Host "  Location : $installerPath" -ForegroundColor Green
        Write-Host "  Size     : $installerSizeMb MB" -ForegroundColor Green
    }
} else {
    Write-Host "[WARN] Inno Setup compiler (ISCC.exe) not found. Binaries staged in $OutputDir." -ForegroundColor Yellow
}

# Start resident background instance in current interactive user desktop session
$stagedExe = (Resolve-Path (Join-Path $targetPublishDir "WinPreview.exe")).Path
if (Test-Path $stagedExe) {
    Start-Process -FilePath $stagedExe -ArgumentList "--background"
    Write-Host "  Started resident background engine ($stagedExe)" -ForegroundColor Green
}

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host "  WinPreview packaging completed successfully!            " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
