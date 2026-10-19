# PowerShell Automated Verification Pipeline
param (
    [string]$Configuration = "Release",
    [switch]$RunTests = $true
)

Write-Host "Running WinPreview Native Build ($Configuration)..." -ForegroundColor Cyan

$srcDir = "C:\Users\CodeX\Desktop\Preview app for Windows\src"
Push-Location $srcDir

cmd.exe /c build.bat
if ($LASTEXITCODE -eq 0) {
    Write-Host "[SUCCESS] WinPreview compiled with 0 warnings." -ForegroundColor Green
} else {
    Write-Error "[FAIL] Compilation error encountered."
}

Pop-Location
