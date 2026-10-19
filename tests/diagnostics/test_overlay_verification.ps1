Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Test {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);

    [DllImport("user32.dll")]
    public static extern IntPtr FindWindowEx(IntPtr parentHandle, IntPtr childAfter, string className, string windowTitle);
}
"@

[Win32Test]::SetProcessDpiAwarenessContext((New-Object IntPtr(-4))) | Out-Null

$artifactDir = "C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336"

function Test-MediaOverlay([string]$filePath, [string]$testName, [string]$screenshotName) {
    Write-Host "`n--- Testing: $testName ---" -ForegroundColor Cyan
    Write-Host "File: $filePath"

    $p = Start-Process -FilePath "src\WinPreview.exe" -ArgumentList "`"$filePath`"" -PassThru
    Start-Sleep -Milliseconds 1200
    $p.Refresh()
    $h = $p.MainWindowHandle

    [Win32Test]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 200

    $winRc = New-Object Win32Test+RECT
    $clientRc = New-Object Win32Test+RECT
    [Win32Test]::GetWindowRect($h, [ref]$winRc) | Out-Null
    [Win32Test]::GetClientRect($h, [ref]$clientRc) | Out-Null

    $winW = $winRc.Right - $winRc.Left
    $winH = $winRc.Bottom - $winRc.Top
    $clientW = $clientRc.Right - $clientRc.Left
    $clientH = $clientRc.Bottom - $clientRc.Top

    Write-Host "Window Size: ${winW}x${winH} | Client Size: ${clientW}x${clientH}"
    $aspect = [float]$clientW / [float]$clientH
    Write-Host ("Client Aspect Ratio: {0:N3}" -f $aspect)

    # Capture window screenshot
    $bmp = New-Object System.Drawing.Bitmap($winW, $winH)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($winRc.Left, $winRc.Top, 0, 0, (New-Object System.Drawing.Size($winW, $winH)))
    $outPath = Join-Path $artifactDir $screenshotName
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Saved screenshot to: $outPath" -ForegroundColor Green

    # Cleanup process
    Stop-Process -Id $p.Id -Force
    Start-Sleep -Milliseconds 300
}

# 1. Test Image Mode (Vertical Reel 9:16)
Test-MediaOverlay "samples\reel-vertical-9-16.png" "Vertical Reel Image" "verify_reel_overlay.png"

# 2. Test Image Mode (Landscape 16:9)
Test-MediaOverlay "samples\landscape-16-9.png" "Landscape Image" "verify_landscape_overlay.png"

# 3. Test Video Mode (Exact user reel video)
$videoPath = "C:\Users\CodeX\Downloads\snapsave-app_3472962196634865774_58117312246.mp4"
if (Test-Path $videoPath) {
    Test-MediaOverlay $videoPath "User Reel Video" "verify_video_reel_overlay.png"
}
