Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCaptureRefined {
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$exe = "C:\Users\CodeX\Desktop\Preview app for Windows\src\WinPreview.exe"
$artifactDir = "C:\Users\CodeX\.gemini\antigravity\brain\c23f9477-1177-4a7a-94f8-431848488336"

function Capture-With-PrintWindow([string]$filePath, [string]$outputName) {
    Write-Host "Capturing $filePath -> $outputName" -ForegroundColor Cyan
    Get-Process -Name 'WinPreview' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300

    $fullPath = (Resolve-Path $filePath).Path
    $proc = Start-Process -FilePath $exe -ArgumentList "`"$fullPath`"" -PassThru
    Start-Sleep -Milliseconds 1200

    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    $hwnd = $p.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) {
        $hwnd = [WinCaptureRefined]::FindWindowW("WinPreviewWindowClass", $null)
    }

    if ($hwnd -ne [IntPtr]::Zero) {
        $rect = New-Object WinCaptureRefined+RECT
        [WinCaptureRefined]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
        $w = $rect.Right - $rect.Left
        $h = $rect.Bottom - $rect.Top

        if ($w -gt 50 -and $h -gt 50) {
            $bmp = New-Object System.Drawing.Bitmap($w, $h)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $hdc = $g.GetHdc()
            $ok = [WinCaptureRefined]::PrintWindow($hwnd, $hdc, 2) # PW_RENDERFULLCONTENT
            $g.ReleaseHdc($hdc)

            $outPath = Join-Path $artifactDir $outputName
            $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
            $g.Dispose()
            $bmp.Dispose()
            $fileLen = (Get-Item $outPath).Length
            Write-Host "  Success: $ok, Size: $fileLen bytes ($w x $h) saved to $outPath" -ForegroundColor Green
        }
    }

    [WinCaptureRefined]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 300
    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}

Capture-With-PrintWindow "samples\Dockerfile" "verify_extensionless_text.png"
Capture-With-PrintWindow "samples\firmware_blob.bin" "verify_unsupported_hex.png"
Capture-With-PrintWindow "samples\data_pipe.psv" "verify_spreadsheet_psv.png"
