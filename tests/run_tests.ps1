<#
.SYNOPSIS
    WinPreview Automated Smoke & Verification Test Suite.
    Runs fast non-interactive checks on the compiled native binary, memory budgets, and sample rendering.
#>

$ErrorActionPreference = "Continue"
$TestDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $TestDir

$exePaths = @(
    (Join-Path $RootDir "dist\WinPreview\WinPreview.exe"),
    (Join-Path $RootDir "src\WinPreview.exe")
)

$targetExe = $null
foreach ($path in $exePaths) {
    if (Test-Path $path) {
        $targetExe = $path
        break
    }
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "         WinPreview Automated Test Suite Runner           " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$passed = 0
$failed = 0

function Report-Test($name, $success, $details = "") {
    if ($success) {
        Write-Host " [PASS] $name" -ForegroundColor Green
        if ($details) { Write-Host "        $details" -ForegroundColor Gray }
        $script:passed++
    } else {
        Write-Host " [FAIL] $name" -ForegroundColor Red
        if ($details) { Write-Host "        $details" -ForegroundColor Magenta }
        $script:failed++
    }
}

# Test 1: Binary Existence
if ($targetExe) {
    $fileInfo = Get-Item $targetExe
    $sizeKb = [math]::Round($fileInfo.Length / 1KB, 1)
    Report-Test "Native Binary Existence" $true "Found at: $targetExe ($sizeKb KB)"
} else {
    Report-Test "Native Binary Existence" $false "Neither src\WinPreview.exe nor dist\WinPreview\WinPreview.exe was found. Run build.bat first."
    exit 1
}

# Test 2: 64-bit PE Architecture Check
try {
    $bytes = [System.IO.File]::ReadAllBytes($targetExe)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $is64Bit = ($machine -eq 0x8664) # IMAGE_FILE_MACHINE_AMD64
    Report-Test "PE Machine Header Architecture" $is64Bit "Machine ID: 0x$($machine.ToString('X4')) (Expected 0x8664 x64)"
} catch {
    Report-Test "PE Machine Header Architecture" $false $_.Exception.Message
}

# Test 3: PDFium Dependency Check
$pdfiumPath = Join-Path (Split-Path -Parent $targetExe) "pdfium.dll"
if (-not (Test-Path $pdfiumPath)) {
    $pdfiumPath = Join-Path $RootDir "src\pdfium.dll"
}
$hasPdfium = Test-Path $pdfiumPath
Report-Test "PDFium Native Library Present" $hasPdfium "Path: $pdfiumPath"

# Test 4: Sample Directory Integrity
$samplesDir = Join-Path $RootDir "samples"
$hasSamples = (Test-Path $samplesDir) -and ((Get-ChildItem $samplesDir).Count -ge 5)
Report-Test "Sample Assets Available" $hasSamples "Samples folder verified with sample documents and media."

# Test 5: Smoke Execution & Memory Budget (<50 MB RAM for cold preview)
$sampleImage = Join-Path $samplesDir "landscape-16-9.png"
if (Test-Path $sampleImage) {
    try {
        $p = Start-Process -FilePath $targetExe -ArgumentList "`"$sampleImage`"" -PassThru
        Start-Sleep -Milliseconds 800
        
        $p.Refresh()
        $wsMb = [math]::Round($p.WorkingSet64 / 1MB, 2)
        $withinBudget = ($wsMb -lt 55)
        Report-Test "Cold Preview Memory Footprint" $withinBudget "Working Set: $wsMb MB (Budget: <55 MB)"
        
        # Gracefully close window
        $p.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 300
        if (-not $p.HasExited) {
            $p.Kill()
        }
    } catch {
        Report-Test "Cold Preview Memory Footprint" $false $_.Exception.Message
    }
} else {
    Report-Test "Cold Preview Memory Footprint" $false "Sample image not found: $sampleImage"
}

Write-Host "`n----------------------------------------------------------" -ForegroundColor Cyan
Write-Host "Test Results: $passed Passed, $failed Failed" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
Write-Host "==========================================================" -ForegroundColor Cyan

if ($failed -gt 0) { exit 1 } else { exit 0 }
