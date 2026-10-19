Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$exe = 'c:\Users\CodeX\Desktop\Preview app for Windows\src\WinPreview.exe'
$img = 'c:\Users\CodeX\Desktop\Preview app for Windows\samples\reel-vertical-9-16.png'
$outShot = 'C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336\reel_dock_capture.png'

$p = Start-Process -FilePath $exe -ArgumentList "`"$img`"" -PassThru
Start-Sleep -Milliseconds 1200

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinUtil {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@

$p.Refresh()
$hwnd = $p.MainWindowHandle
[WinUtil]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

$rc = New-Object WinUtil+RECT
[WinUtil]::GetWindowRect($hwnd, [ref]$rc) | Out-Null
$w = $rc.Right - $rc.Left
$h = $rc.Bottom - $rc.Top

if ($w -gt 50 -and $h -gt 50) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($rc.Left, $rc.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $bmp.Save($outShot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Captured screenshot to $outShot ($w x $h)"
}

Stop-Process -Id $p.Id -Force
