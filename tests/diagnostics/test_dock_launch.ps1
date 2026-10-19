$exe = 'c:\Users\CodeX\Desktop\Preview app for Windows\src\WinPreview.exe'
$img = 'c:\Users\CodeX\Desktop\Preview app for Windows\samples\reel-vertical-9-16.png'
$pdf = 'c:\Users\CodeX\Desktop\Preview app for Windows\samples\preview-showcase.pdf'

Write-Host "--- Test 1: Image Launch ---" -ForegroundColor Cyan
$p1 = Start-Process -FilePath $exe -ArgumentList "`"$img`"" -PassThru
Start-Sleep -Milliseconds 1200
$p1Fresh = Get-Process -Id $p1.Id -ErrorAction SilentlyContinue
if ($p1Fresh -and -not $p1Fresh.HasExited) {
    Write-Host "Image launch SUCCESS: Process ID $($p1.Id), Memory: $([math]::Round($p1Fresh.WorkingSet64 / 1MB, 2)) MB" -ForegroundColor Green
    Stop-Process -Id $p1.Id -Force
} else {
    Write-Host "Image launch FAILED: Process exited unexpectedly" -ForegroundColor Red
}

Write-Host "`n--- Test 2: PDF Launch ---" -ForegroundColor Cyan
$p2 = Start-Process -FilePath $exe -ArgumentList "`"$pdf`"" -PassThru
Start-Sleep -Milliseconds 1200
$p2Fresh = Get-Process -Id $p2.Id -ErrorAction SilentlyContinue
if ($p2Fresh -and -not $p2Fresh.HasExited) {
    Write-Host "PDF launch SUCCESS: Process ID $($p2.Id), Memory: $([math]::Round($p2Fresh.WorkingSet64 / 1MB, 2)) MB" -ForegroundColor Green
    Stop-Process -Id $p2.Id -Force
} else {
    Write-Host "PDF launch FAILED: Process exited unexpectedly" -ForegroundColor Red
}

Write-Host "`nAll tests completed!" -ForegroundColor Green
