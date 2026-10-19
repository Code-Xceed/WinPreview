Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap {
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Start-Process "src\WinPreview.exe" -ArgumentList "`"C:\Users\CodeX\Downloads\Overthinking.jpg`"" -PassThru
Start-Sleep -Milliseconds 1200
$p.Refresh()
$rc = New-Object WinCap+RECT
[WinCap]::GetWindowRect($p.MainWindowHandle, [ref]$rc) | Out-Null
$w = $rc.Right - $rc.Left
$h = $rc.Bottom - $rc.Top
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$res = [WinCap]::PrintWindow($p.MainWindowHandle, $hdc, 2) # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$g.Dispose()
$outPath = "C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336\test_printwindow.png"
$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Stop-Process -Id $p.Id -Force
Write-Host "PrintWindow result: $res ($w x $h) saved to $outPath"
