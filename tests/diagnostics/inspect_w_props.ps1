$sh = New-Object -ComObject Shell.Application
$w = $sh.Windows().Item(0)
Write-Host "HWND: $($w.HWND)"
Write-Host "HWND hex: 0x$($w.HWND.ToString('X'))"
Write-Host "LocationName: $($w.LocationName)"
Write-Host "LocationURL: $($w.LocationURL)"
Write-Host "TopLevelContainer: $($w.TopLevelContainer)"
Write-Host "Parent: $($w.Parent)"

# Let's inspect properties of w
$w | Get-Member
