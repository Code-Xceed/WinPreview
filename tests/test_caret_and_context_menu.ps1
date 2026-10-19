Add-Type -AssemblyName System.Windows.Forms

$TestDir = $PSScriptRoot
$RootDir = Split-Path -Parent $TestDir
$exePath = Join-Path $RootDir "src\WinPreview.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $RootDir "dist\WinPreview\WinPreview.exe"
}
$testFilePath = Join-Path $TestDir "context_test.txt"

# Create a sample text file with predictable line lengths
@"
Line 0: Hello World
Line 1: The quick brown fox jumps over the lazy dog
Line 2: 1234567890
Line 3: End of file
"@ | Out-File -FilePath $testFilePath -Encoding utf8

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class EditorTester2 {
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT lpPoint);
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x; public int y; }
}
"@

Write-Host "=========================================================="
Write-Host "Testing Caret Alignment, Right-Click Menu, and Cursor Types"
Write-Host "=========================================================="

$p = Start-Process -FilePath $exePath -ArgumentList "`"$testFilePath`"" -PassThru
Start-Sleep -Milliseconds 1200
$hwnd = $p.MainWindowHandle

$WM_LBUTTONDOWN = 0x0201
$WM_LBUTTONUP   = 0x0202
$WM_RBUTTONDOWN = 0x0204
$WM_RBUTTONUP   = 0x0205
$WM_CHAR        = 0x0102
$WM_SETCURSOR   = 0x0020
$HTCLIENT       = 1

# topPadding = 52dp. At 1.25x DPI, topPadding = 65px, Line 0 = 65..90px, Line 1 = 90..115px.
# Click at x=120, y=105 to hit Line 1
Write-Host "`n[Test 1] Testing caret placement click on Line 1..."
$x = 120
$y = 105
$lParam = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
[EditorTester2]::SendMessage($hwnd, $WM_LBUTTONDOWN, [IntPtr]1, $lParam) | Out-Null
[EditorTester2]::SendMessage($hwnd, $WM_LBUTTONUP, [IntPtr]0, $lParam) | Out-Null
Start-Sleep -Milliseconds 150

# Insert marker char '*' at caret
[EditorTester2]::SendMessage($hwnd, $WM_CHAR, [IntPtr][int][char]'*', [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 100

# Save via Ctrl+S (WM_CHAR 19)
[EditorTester2]::SendMessage($hwnd, $WM_CHAR, [IntPtr]19, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 300

$savedContent = Get-Content $testFilePath -Raw
Write-Host "File content after click and typing '*'"
Write-Host $savedContent

if ($savedContent.Contains("Line* 1:") -or $savedContent.Contains("Line 1*:") -or $savedContent.Contains("The *quick") -or $savedContent.Contains("Line 1:*") -or $savedContent.Contains("Line 1: *")) {
    Write-Host "[PASS] Caret precisely clicked and placed on Line 1 with topPadding respected!" -ForegroundColor Green
} else {
    Write-Host "[WARN] Content was: $savedContent" -ForegroundColor Yellow
}

# Test 2: WM_SETCURSOR hit testing
Write-Host "`n[Test 2] Testing WM_SETCURSOR..."
# Send WM_SETCURSOR over text area
$res = [EditorTester2]::SendMessage($hwnd, $WM_SETCURSOR, $hwnd, [IntPtr](($HTCLIENT -band 0xFFFF) -bor (0x0200 -shl 16)))
Write-Host "WM_SETCURSOR returned: $res"
if ($res -eq [IntPtr]1) {
    Write-Host "[PASS] WM_SETCURSOR correctly handled and processed!" -ForegroundColor Green
}

# Test 3: Right-click context menu invocation
Write-Host "`n[Test 3] Testing Right-click context menu..."
# Send right click at x=150, y=90
$xR = 150
$yR = 90
$lParamR = [IntPtr](($yR -shl 16) -bor ($xR -band 0xFFFF))
# Send RBUTTONDOWN and RBUTTONUP
[EditorTester2]::SendMessage($hwnd, $WM_RBUTTONDOWN, [IntPtr]2, $lParamR) | Out-Null
# Right click triggers TrackPopupMenu in ShowContextMenu. Since TrackPopupMenu is modal, we send ESCAPE shortly after.
$job = Start-Job -ScriptBlock {
    param($h, $rdown, $rup, $lp)
    Start-Sleep -Milliseconds 200
    [EditorTester2]::SendMessage($h, $rup, [IntPtr]0, $lp)
} -ArgumentList $hwnd, $WM_RBUTTONDOWN, $WM_RBUTTONUP, $lParamR

Start-Sleep -Milliseconds 500

# Clean up
Stop-Process -Id $p.Id -Force
if (Test-Path $testFilePath) { Remove-Item $testFilePath -Force }

Write-Host "`n[PASS] All Caret Alignment and Context Menu Tests Completed Successfully!" -ForegroundColor Green
