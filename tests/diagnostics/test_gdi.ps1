Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinGdi {
    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("gdi32.dll")]
    public static extern bool BitBlt(IntPtr hObject, int nXDest, int nYDest, int nWidth, int nHeight, IntPtr hObjectSource, int nXSrc, int nYSrc, int dwRop);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Start-Process -FilePath "src\WinPreview.exe" -ArgumentList "samples\reel-vertical-9-16.png" -PassThru
Start-Sleep -Milliseconds 1200
$p.Refresh()
$hwnd = $p.MainWindowHandle
$rc = New-Object WinGdi+RECT
[WinGdi]::GetClientRect($hwnd, [ref]$rc) | Out-Null
$w = $rc.Right - $rc.Left
$h = $rc.Bottom - $rc.Top
$hdcSrc = [WinGdi]::GetDC($hwnd)
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdcDest = $g.GetHdc()
$res = [WinGdi]::BitBlt($hdcDest, 0, 0, $w, $h, $hdcSrc, 0, 0, 0x00CC0020)
$g.ReleaseHdc($hdcDest)
$g.Dispose()
[WinGdi]::ReleaseDC($hwnd, $hdcSrc) | Out-Null
$bmp.Save("C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336\verify_reel_gdi.png", [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Stop-Process -Id $p.Id -Force
Write-Host "BitBlt result: $res (${w}x${h})"
