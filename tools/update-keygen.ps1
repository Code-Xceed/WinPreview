# Generates the ECDSA P-256 key pair used to sign update manifests.
#
# The private key is written outside the repository (never commit it) and must be
# stored as the GitHub Actions secret WINPREVIEW_UPDATE_KEY (base64 CNG ECC
# private blob). The public blob is printed for embedding in the app.
#
#   powershell -ExecutionPolicy Bypass -File tools\update-keygen.ps1
param(
    [string]$PrivateKeyPath = (Join-Path $env:LOCALAPPDATA "WinPreview\keys\update-signing-key.txt")
)

$ErrorActionPreference = "Stop"

$creationParams = New-Object System.Security.Cryptography.CngKeyCreationParameters
$creationParams.KeyUsage = [System.Security.Cryptography.CngKeyUsages]::Signing
$creationParams.ExportPolicy = [System.Security.Cryptography.CngExportPolicies]::AllowExport -bor [System.Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
$key = [System.Security.Cryptography.CngKey]::Create(
    [System.Security.Cryptography.CngAlgorithm]::ECDsaP256,
    $null,
    $creationParams)
$privateBlob = $key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$publicBlob  = $key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
$key.Dispose()

$dir = Split-Path -Parent $PrivateKeyPath
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
Set-Content -Path $PrivateKeyPath -Value ([Convert]::ToBase64String($privateBlob)) -Encoding ASCII

Write-Host ""
Write-Host "Private key written to: $PrivateKeyPath" -ForegroundColor Green
Write-Host "Add it as the repository secret WINPREVIEW_UPDATE_KEY (base64, single line)." -ForegroundColor Yellow
Write-Host ""
Write-Host "Public key (embed in src/update_engine.cpp as kUpdatePublicKeyBlobBase64):" -ForegroundColor Cyan
Write-Host ([Convert]::ToBase64String($publicBlob))
Write-Host ""

# Round-trip check so a broken key is caught immediately.
$imported = [System.Security.Cryptography.CngKey]::Import($privateBlob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$ecdsa = New-Object System.Security.Cryptography.ECDsaCng($imported)
$probe = [Text.Encoding]::UTF8.GetBytes("winpreview-update-signature-probe")
$sig = $ecdsa.SignData($probe, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
$ok = $ecdsa.VerifyData($probe, $sig, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
Write-Host ("Signature self-check: " + $(if ($ok) { "OK" } else { "FAILED" }))
Write-Host ("Signature length: " + $sig.Length + " bytes")
$ecdsa.Dispose()
$imported.Dispose()
