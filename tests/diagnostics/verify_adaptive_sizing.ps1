Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
}
"@

[Win32]::SetProcessDpiAwarenessContext((New-Object IntPtr(-4))) | Out-Null # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

$tests = @(
    @{ Name = "Vertical Reel (9:16)"; File = "samples\reel-vertical-9-16.png"; MediaW = 1080; MediaH = 1920 },
    @{ Name = "Square (1:1)"; File = "samples\square-1-1.png"; MediaW = 1080; MediaH = 1080 },
    @{ Name = "Landscape (16:9)"; File = "samples\landscape-16-9.png"; MediaW = 1920; MediaH = 1080 },
    @{ Name = "Wide Banner (3:1)"; File = "samples\wide-banner.png"; MediaW = 2400; MediaH = 800 }
)

$userVideo = "C:\Users\CodeX\Downloads\snapsave-app_3472962196634865774_58117312246.mp4"
if (Test-Path $userVideo) {
    $tests += @{ Name = "Reel Video (MP4 9:16)"; File = $userVideo; MediaW = 1080; MediaH = 1920 }
}

$userPoster = "C:\Users\CodeX\Downloads\Overthinking.jpg"
if (Test-Path $userPoster) {
    # File is 1080x1350 with 90px black bars on left/right. Auto-trim adapts window to 900x1350 (2:3 aspect)
    $tests += @{ Name = "Poster (Auto-Trimmed 2:3)"; File = $userPoster; MediaW = 900; MediaH = 1350 }
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "WINPREVIEW ADAPTIVE MEDIA WINDOW SIZING VERIFICATION" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

foreach ($t in $tests) {
    $fullPath = (Resolve-Path $t.File).Path
    $p = Start-Process -FilePath "src\WinPreview.exe" -ArgumentList "`"$fullPath`"" -PassThru
    
    # Wait for window to initialize and adapt to media
    Start-Sleep -Milliseconds 1200
    $p.Refresh()
    $h = $p.MainWindowHandle

    $winRc = New-Object Win32+RECT
    $clientRc = New-Object Win32+RECT
    [Win32]::GetWindowRect($h, [ref]$winRc) | Out-Null
    [Win32]::GetClientRect($h, [ref]$clientRc) | Out-Null

    $winW = $winRc.Right - $winRc.Left
    $winH = $winRc.Bottom - $winRc.Top
    $clientW = $clientRc.Right - $clientRc.Left
    $clientH = $clientRc.Bottom - $clientRc.Top
    $canvasH = $clientH # Dock is now a floating frosted glass overlay directly ON TOP of media - zero black bars!

    $expectedAspect = [float]$t.MediaW / [float]$t.MediaH
    $actualCanvasAspect = [float]$clientW / [float]$canvasH
    $aspectDiff = [Math]::Abs($expectedAspect - $actualCanvasAspect)

    $pass = ($aspectDiff -lt 0.02)

    Write-Host "`nTest: $($t.Name)" -ForegroundColor Yellow
    Write-Host "  File: $($t.File) ($($t.MediaW)x$($t.MediaH))"
    Write-Host "  Window Total: ${winW}x${winH} | Client Area: ${clientW}x${clientH}"
    Write-Host "  Media Canvas Area: ${clientW}x${canvasH}"
    Write-Host "  Target Aspect: $([Math]::Round($expectedAspect, 4)) | Canvas Aspect: $([Math]::Round($actualCanvasAspect, 4))"
    Write-Host "  Aspect Delta: $([Math]::Round($aspectDiff, 5))"

    if ($pass) {
        Write-Host "  [PASS] Window perfectly sized to media with ZERO black bars!" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] Aspect mismatch!" -ForegroundColor Red
    }

    Stop-Process -Id $p.Id -Force
}
