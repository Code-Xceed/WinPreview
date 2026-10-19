Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$exe = 'c:\Users\CodeX\Desktop\Preview app for Windows\src\WinPreview.exe'
$img = 'c:\Users\CodeX\Desktop\Preview app for Windows\samples\reel-vertical-9-16.png'
$outFit = 'C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336\verify_hover_fit.png'
$outRot = 'C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336\verify_hover_rotate.png'

$code = @'
using System;
using System.Runtime.InteropServices;
public class WinDpiTester {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
'@
Add-Type -TypeDefinition $code

$p = Start-Process -FilePath $exe -ArgumentList $img -PassThru
Start-Sleep -Milliseconds 1200

$p.Refresh()
$hwnd = $p.MainWindowHandle
[WinDpiTester]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

$dpi = [WinDpiTester]::GetDpiForWindow($hwnd)
if ($dpi -eq 0) { $dpi = 96 }
$dpiScale = $dpi / 96.0
Write-Host "Window DPI: $dpi ($dpiScale)"

$clientRc = New-Object WinDpiTester+RECT
[WinDpiTester]::GetClientRect($hwnd, [ref]$clientRc) | Out-Null
$clientWPhys = $clientRc.Right - $clientRc.Left
$clientHPhys = $clientRc.Bottom - $clientRc.Top
$clientWDip = $clientWPhys / $dpiScale
$clientHDip = $clientHPhys / $dpiScale
Write-Host "Client Physical: ${clientWPhys}x${clientHPhys}, DIP: ${clientWDip}x${clientHDip}"

# Calculate Image Mode Dock
$dockW = 357.0
if ($clientWDip -lt 370.0) { $dockW = 295.0 }
$dockX = ($clientWDip - $dockW) / 2.0
$curX = $dockX + 5.0

# Zoom capsule: 106px, Divider: 17px, Fit button: 30px
$fitLeftDip = $curX + 106.0 + 17.0
$fitRightDip = $fitLeftDip + 30.0
$fitCenterDipX = ($fitLeftDip + $fitRightDip) / 2.0
$fitCenterDipY = 26.0

$fitCenterPhysX = [int][math]::Round($fitCenterDipX * $dpiScale)
$fitCenterPhysY = [int][math]::Round($fitCenterDipY * $dpiScale)

Write-Host "Targeting Fit button at Physical: ($fitCenterPhysX, $fitCenterPhysY), DIP: ($fitCenterDipX, $fitCenterDipY)"

$pt = New-Object WinDpiTester+POINT
$pt.X = $fitCenterPhysX
$pt.Y = $fitCenterPhysY
[WinDpiTester]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
[WinDpiTester]::SetCursorPos($pt.X, $pt.Y) | Out-Null

$lpVal = [int64](($fitCenterPhysY * 65536) + ($fitCenterPhysX -band 0xFFFF))
$lParam = [IntPtr]$lpVal
[WinDpiTester]::SendMessage($hwnd, 0x0200, [IntPtr]::Zero, $lParam) | Out-Null

Start-Sleep -Milliseconds 700

$winRc = New-Object WinDpiTester+RECT
[WinDpiTester]::GetWindowRect($hwnd, [ref]$winRc) | Out-Null
$w = $winRc.Right - $winRc.Left
$h = $winRc.Bottom - $winRc.Top

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($winRc.Left, $winRc.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$bmp.Save($outFit, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Host "Saved Fit screenshot"

# Rotate Left button: fitRightDip + 17px divider + 15px center
$rotLeftDip = $fitRightDip + 17.0
$rotCenterDipX = $rotLeftDip + 15.0
$rotCenterPhysX = [int][math]::Round($rotCenterDipX * $dpiScale)

Write-Host "Targeting Rotate Left button at Physical: ($rotCenterPhysX, $fitCenterPhysY), DIP: ($rotCenterDipX, $fitCenterDipY)"

$ptRot = New-Object WinDpiTester+POINT
$ptRot.X = $rotCenterPhysX
$ptRot.Y = $fitCenterPhysY
[WinDpiTester]::ClientToScreen($hwnd, [ref]$ptRot) | Out-Null
[WinDpiTester]::SetCursorPos($ptRot.X, $ptRot.Y) | Out-Null

$lpValRot = [int64](($fitCenterPhysY * 65536) + ($rotCenterPhysX -band 0xFFFF))
$lParamRot = [IntPtr]$lpValRot
[WinDpiTester]::SendMessage($hwnd, 0x0200, [IntPtr]::Zero, $lParamRot) | Out-Null

Start-Sleep -Milliseconds 700

$bmpRot = New-Object System.Drawing.Bitmap($w, $h)
$gRot = [System.Drawing.Graphics]::FromImage($bmpRot)
$gRot.CopyFromScreen($winRc.Left, $winRc.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$bmpRot.Save($outRot, [System.Drawing.Imaging.ImageFormat]::Png)
$gRot.Dispose()
$bmpRot.Dispose()
Write-Host "Saved Rotate screenshot"

Stop-Process -Id $p.Id -Force
Write-Host "Done"
