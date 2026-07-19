$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

$certName = "DataScythe Local Code Signing (2026)"
$publicCert = Join-Path $root "docs\DataScythe_Local_Code_Signing_2026.cer"
$privateDir = Join-Path $root ".gnupg-release\private-keys-v1.d"
$pfxPath = Join-Path $privateDir "DataScythe_Local_Code_Signing_2026.pfx"
$p12Path = Join-Path $privateDir "DataScythe_Local_Code_Signing_2026.p12"
# X.509 always has NotAfter; use Windows practical maximum (suite-wide "no expiry")
$never = [datetime]"9999-12-31"

New-Item -ItemType Directory -Force -Path $privateDir | Out-Null

if (Test-Path -LiteralPath $publicCert) {
    Write-Host "Public certificate already exists: $publicCert"
    Write-Host "Delete docs/DataScythe_Local_Code_Signing_2026.cer to regenerate."
    exit 0
}

Write-Host "[RUN] Creating self-signed code-signing certificate"
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=$certName" `
    -FriendlyName $certName `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -KeyExportPolicy Exportable `
    -KeySpec Signature `
    -KeyLength 4096 `
    -HashAlgorithm SHA256 `
    -NotAfter $never

try {
    Export-Certificate -Cert $cert -FilePath $publicCert | Out-Null
    # Empty password — same as PassMan / YellowSphere / ShadowVault / etc.
    Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password ([securestring]::new()) | Out-Null
    Copy-Item -LiteralPath $pfxPath -Destination $p12Path -Force
}
finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -Force -ErrorAction SilentlyContinue
}

& (Join-Path $PSScriptRoot "hide_gnupg_release.ps1") | Out-Null

Write-Host "[PASS] Code-signing certificate created"
Write-Host "Public:  docs/DataScythe_Local_Code_Signing_2026.cer"
Write-Host "Private: .gnupg-release/private-keys-v1.d/DataScythe_Local_Code_Signing_2026.pfx"
Write-Host "Private: .gnupg-release/private-keys-v1.d/DataScythe_Local_Code_Signing_2026.p12"
Write-Host "PFX password: (empty)"
Write-Host "Thumbprint: $($cert.Thumbprint)"
