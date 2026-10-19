$sh = New-Object -ComObject Shell.Application
$windows = $sh.Windows()
Write-Host "Shell.Application Windows count: $($windows.Count)"
for ($i = 0; $i -lt $windows.Count; $i++) {
    $w = $windows.Item($i)
    Write-Host "[$i] HWND=$($w.HWND) Name=$($w.LocationName) URL=$($w.LocationURL)"
    try {
        $doc = $w.Document
        if ($doc) {
            $sel = $doc.SelectedItems()
            Write-Host "   Selected Count: $($sel.Count)"
            for ($j = 0; $j -lt $sel.Count; $j++) {
                Write-Host "   Item[$j]: $($sel.Item($j).Path)"
            }
            if ($sel.Count -eq 0) {
                Write-Host "   Focused Item: $($doc.FocusedItem.Path)"
            }
        }
    } catch {
        Write-Host "   Error reading document: $_"
    }
}
