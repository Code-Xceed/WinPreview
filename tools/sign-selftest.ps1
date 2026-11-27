<#
.SYNOPSIS
    Proves the release signing pipeline without a CA certificate.

.DESCRIPTION
    Creates a temporary self-signed code-signing certificate, trusts it for the
    current user (test only), runs publish.ps1 -RequireSignature so both the app
    executable and the installer are signed and timestamped, verifies the
    signatures, then removes the certificate and the temporary files.

    This validates plumbing only. SmartScreen reputation and trusted publisher
    names require a certificate from a recognised CA (OV/EV) or SignPath.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\sign-selftest.ps1

.NOTES
    Requires signtool.exe from the Windows SDK signing tools.
#>
param(
    [string]$Password = "WinPreview-SelfTest-Only",
    [switch]$KeepCertificate
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$pfx = Join-Path $env:TEMP "winpreview-selftest.pfx"
$cer = Join-Path $env:TEMP "winpreview-selftest.cer"
$cert = $null

function Remove-TestCertificate {
    param($Certificate)
    if ($KeepCertificate -or -not $Certificate) { return }
    Remove-Item "Cert:\CurrentUser\Root\$($Certificate.Thumbprint)" -ErrorAction SilentlyContinue
    Remove-Item "Cert:\CurrentUser\My\$($Certificate.Thumbprint)" -ErrorAction SilentlyContinue
    Remove-Item $pfx, $cer -ErrorAction SilentlyContinue
}

try {
    if (-not (Get-Command "New-SelfSignedCertificate" -ErrorAction SilentlyContinue)) {
        throw "New-SelfSignedCertificate is unavailable (PowerShell 5.1+ on Windows 10/11 is required)."
    }

    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=WinPreview Signing Self-Test" `
        -KeyUsage DigitalSignature `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -NotAfter (Get-Date).AddDays(2)
    Write-Host "Temporary certificate: $($cert.Thumbprint)" -ForegroundColor DarkGray

    $secure = ConvertTo-SecureString -String $Password -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    # Trust the self-signed root so the signature chain verifies as Valid.
    Import-Certificate -FilePath $cer -CertStoreLocation "Cert:\CurrentUser\Root" | Out-Null

    $env:WINPREVIEW_SIGN_PFX = $pfx
    $env:WINPREVIEW_SIGN_PFX_PASSWORD = $Password

    Push-Location $Root
    try {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "publish.ps1") `
            -SkipTests -NoLaunch -RequireSignature
        if ($LASTEXITCODE -ne 0) { throw "publish.ps1 -RequireSignature failed (exit $LASTEXITCODE)." }
    } finally {
        Pop-Location
    }

    foreach ($relative in @("dist\WinPreview\WinPreview.exe", "dist\Installer\WinPreview-Setup-x64.exe")) {
        $path = Join-Path $Root $relative
        if (-not (Test-Path $path)) { throw "Expected artifact missing: $relative" }
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne "Valid") {
            throw "$relative did not verify: $($signature.Status) $($signature.StatusMessage)"
        }
        Write-Host "Verified: $relative ($($signature.SignerCertificate.Subject))" -ForegroundColor Green
    }

    Write-Host ""
    Write-Host "Signing self-test passed. The pipeline signs and verifies both artifacts (timestamps need a TSA; CI uses signtool for RFC 3161)." -ForegroundColor Green
    Write-Host "A trusted publisher still requires a CA certificate or SignPath; see docs/update-system-v2-plan.md." -ForegroundColor Yellow
    exit 0
} catch {
    Write-Host "Signing self-test failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    Remove-Item Env:\WINPREVIEW_SIGN_PFX -ErrorAction SilentlyContinue
    Remove-Item Env:\WINPREVIEW_SIGN_PFX_PASSWORD -ErrorAction SilentlyContinue
    Remove-TestCertificate $cert
}
