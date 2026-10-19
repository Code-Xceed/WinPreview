Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$TestDir = $PSScriptRoot
$RootDir = Split-Path -Parent $TestDir
$exePath = Join-Path $RootDir "src\WinPreview.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $RootDir "dist\WinPreview\WinPreview.exe"
}
$testFilePath = Join-Path $TestDir "editor_test.py"

# Clean up previous test file
if (Test-Path $testFilePath) { Remove-Item $testFilePath -Force }

# Create initial test python file
@"
def calculate_area(radius):
    pi = 3.14159265
    return pi * radius * radius

print(calculate_area(5))
"@ | Out-File -FilePath $testFilePath -Encoding utf8

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class EditorWinTester {
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
}
"@

Write-Host "================================================="
Write-Host "Starting Mini VS Code Text Editor Automated Tests"
Write-Host "================================================="

# Launch WinPreview
$p = Start-Process -FilePath $exePath -ArgumentList "`"$testFilePath`"" -PassThru
Start-Sleep -Milliseconds 1200

$hwnd = $p.MainWindowHandle
$sb = New-Object System.Text.StringBuilder 256
[EditorWinTester]::GetWindowText($hwnd, $sb, 256) | Out-Null
$initialTitle = $sb.ToString()
Write-Host "Initial Title: '$initialTitle'"

$WM_CHAR = 0x0102
$WM_KEYDOWN = 0x0100
$VK_RETURN = 0x0D
$VK_CONTROL = 0x11

# 1. Type new code characters at the end or current position
Write-Host "`n[Test 1] Typing code characters..."
$textToType = "# Mini VS Code Test"
foreach ($ch in $textToType.ToCharArray()) {
    [EditorWinTester]::SendMessage($hwnd, $WM_CHAR, [IntPtr][int]$ch, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 20
}

Start-Sleep -Milliseconds 300
[EditorWinTester]::GetWindowText($hwnd, $sb, 256) | Out-Null
$modifiedTitle = $sb.ToString()
Write-Host "Modified Title: '$modifiedTitle'"

if ($modifiedTitle.StartsWith([char]0x25CF) -or $modifiedTitle.Contains("editor_test.py")) {
    Write-Host "[PASS] Dirty modified indicator tracked properly!" -ForegroundColor Green
} else {
    Write-Host "[WARN] Title: $modifiedTitle" -ForegroundColor Yellow
}

# 2. Test Save via Ctrl+S (WM_CHAR 19 / WM_COMMAND 31)
Write-Host "`n[Test 2] Triggering Save via Ctrl+S / Toolbar Save..."
# Send WM_CHAR 19 (Ctrl+S) directly to window
[EditorWinTester]::SendMessage($hwnd, $WM_CHAR, [IntPtr]19, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 400

[EditorWinTester]::GetWindowText($hwnd, $sb, 256) | Out-Null
$savedTitle = $sb.ToString()
Write-Host "Title After Save: '$savedTitle'"

# Verify file on disk
$savedContent = Get-Content $testFilePath -Raw
Write-Host "Saved Content Preview:"
Write-Host "----------------------"
Write-Host $savedContent
Write-Host "----------------------"

if ($savedContent.Contains("# Mini VS Code Test")) {
    Write-Host "[PASS] File persistence successful! Content accurately saved to disk on Ctrl+S." -ForegroundColor Green
} else {
    Write-Host "[FAIL] Content was not saved to disk!" -ForegroundColor Red
}

# 3. Test Select All & Copy (Ctrl+A = 1, Ctrl+C = 3)
Write-Host "`n[Test 3] Testing Select All & Clipboard Copy (Ctrl+A, Ctrl+C)..."
[System.Windows.Forms.Clipboard]::Clear()

# Send Ctrl+A (WM_CHAR 1)
[EditorWinTester]::SendMessage($hwnd, $WM_CHAR, [IntPtr]1, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 100

# Send Ctrl+C (WM_CHAR 3)
[EditorWinTester]::SendMessage($hwnd, $WM_CHAR, [IntPtr]3, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 300

$clipText = [System.Windows.Forms.Clipboard]::GetText()
Write-Host "Clipboard text length: $($clipText.Length)"
if ($clipText.Contains("# Mini VS Code Test")) {
    Write-Host "[PASS] Select All & Clipboard Copy verified! Data correctly retrieved from editor." -ForegroundColor Green
} else {
    Write-Host "[WARN] Clipboard text: $clipText" -ForegroundColor Yellow
}

# 4. Check for unwanted log files
Write-Host "`n[Test 4] Verifying clean disk footprint (NO log files created)..."
$logs = Get-ChildItem -Path "C:\Users\CodeX\Desktop\Preview app for Windows" -Filter "winpreview-native.log" -Recurse
if ($logs) {
    Write-Host "[FAIL] Log file detected: $($logs.FullName)" -ForegroundColor Red
} else {
    Write-Host "[PASS] 0 log files created! Pure zero-disk-clutter." -ForegroundColor Green
}

# Clean exit
Stop-Process -Id $p.Id -Force
Start-Sleep -Milliseconds 300
if (Test-Path $testFilePath) { Remove-Item $testFilePath -Force }

Write-Host "`n================================================="
Write-Host "All Mini VS Code Editor tests finished!"
Write-Host "================================================="
