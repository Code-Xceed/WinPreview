Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$testsDir = $PSScriptRoot
$RootDir = Split-Path -Parent $testsDir
$exePath = Join-Path $RootDir "src\WinPreview.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $RootDir "dist\WinPreview\WinPreview.exe"
}
$artifactDir = Join-Path $testsDir "output"
if (-not (Test-Path $artifactDir)) { New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null }

function Capture-WindowScreenshot($hwnd, $outputPath) {
    Start-Sleep -Milliseconds 300
    
    Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    public class WinCapRunner {
        [StructLayout(LayoutKind.Sequential)]
        public struct RECT {
            public int Left, Top, Right, Bottom;
        }
        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
        [DllImport("user32.dll")]
        public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    }
"@
    
    $rc = New-Object WinCapRunner+RECT
    [WinCapRunner]::GetWindowRect($hwnd, [ref]$rc) | Out-Null
    $w = $rc.Right - $rc.Left
    $h = $rc.Bottom - $rc.Top
    
    if ($w -gt 50 -and $h -gt 50) {
        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $gfx.GetHdc()
        $res = [WinCapRunner]::PrintWindow($hwnd, $hdc, 2) # PW_RENDERFULLCONTENT
        $gfx.ReleaseHdc($hdc)
        $gfx.Dispose()
        $bmp.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host "Captured screenshot: $outputPath ($w x $h, result=$res)" -ForegroundColor Green
    }
}

# 1. Check for any winpreview-native.log
Write-Host "Checking for existing log files..."
$existingLogs = Get-ChildItem -Path "C:\Users\CodeX\Desktop\Preview app for Windows" -Filter "winpreview-native.log" -Recurse
if ($existingLogs) {
    Write-Host "Removing pre-existing log files..."
    $existingLogs | Remove-Item -Force
}

# Test 1: Python Code
Write-Host "`n[Test 1] Testing Python Source Code Preview..."
$pyFile = Join-Path $testsDir "sample.py"
$p1 = Start-Process -FilePath $exePath -ArgumentList "`"$pyFile`"" -PassThru
Start-Sleep -Milliseconds 1200
Capture-WindowScreenshot $p1.MainWindowHandle (Join-Path $artifactDir "verify_text_python.png")
Stop-Process -Id $p1.Id -Force
Start-Sleep -Milliseconds 400

# Test 2: CSV Spreadsheet
Write-Host "`n[Test 2] Testing CSV Spreadsheet Preview..."
$csvFile = Join-Path $testsDir "sales_report.csv"
$p2 = Start-Process -FilePath $exePath -ArgumentList "`"$csvFile`"" -PassThru
Start-Sleep -Milliseconds 1200
Capture-WindowScreenshot $p2.MainWindowHandle (Join-Path $artifactDir "verify_spreadsheet_csv.png")
Stop-Process -Id $p2.Id -Force
Start-Sleep -Milliseconds 400

# Test 3: Audio File
Write-Host "`n[Test 3] Testing Audio Player Preview..."
$audioFile = "C:\Windows\Media\Alarm01.wav"
$p3 = Start-Process -FilePath $exePath -ArgumentList "`"$audioFile`"" -PassThru
Start-Sleep -Milliseconds 1200
Capture-WindowScreenshot $p3.MainWindowHandle (Join-Path $artifactDir "verify_audio_player.png")
Stop-Process -Id $p3.Id -Force
Start-Sleep -Milliseconds 400

# Final Check: Verify NO log files were generated
Write-Host "`nVerifying no winpreview-native.log was created anywhere..."
$newLogs = Get-ChildItem -Path "C:\Users\CodeX\Desktop\Preview app for Windows" -Filter "winpreview-native.log" -Recurse
if ($newLogs) {
    Write-Host "[FAIL] Log file was created: $($newLogs.FullName)" -ForegroundColor Red
} else {
    Write-Host "[PASS] ZERO log files created! Completely clean disk operations." -ForegroundColor Green
}
