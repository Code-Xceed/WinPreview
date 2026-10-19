Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinSnap {
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBmp, uint nFlags);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

[WinSnap]::SetProcessDpiAwarenessContext((New-Object IntPtr(-4))) | Out-Null

$img = "c:\Users\CodeX\Desktop\Preview app for Windows\samples\reel-vertical-9-16.png"
$outShot = "C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336\verify_reel_overlay.png"

$p = Start-Process -FilePath "src\WinPreview.exe" -ArgumentList "`"$img`"" -PassThru
Start-Sleep -Milliseconds 1200
$p.Refresh()
$hwnd = $p.MainWindowHandle

$rc = New-Object WinSnap+RECT
$cl = New-Object WinSnap+RECT
[WinSnap]::GetWindowRect($hwnd, [ref]$rc) | Out-Null
[WinSnap]::GetClientRect($hwnd, [ref]$cl) | Out-Null
$w = $rc.Right - $rc.Left
$h = $rc.Bottom - $rc.Top
$cw = $cl.Right - $cl.Left
$ch = $cl.Bottom - $cl.Top

Write-Host "Window: ${w}x${h} | Client: ${cw}x${ch} (Ratio: $([float]$cw / [float]$ch))"

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[WinSnap]::PrintWindow($hwnd, $hdc, 2) | Out-Null # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$g.Dispose()

$bmp.Save($outShot, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Stop-Process -Id $p.Id -Force
Write-Host "Captured screenshot to $outShot"
