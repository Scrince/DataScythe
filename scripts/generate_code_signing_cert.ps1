$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

$certName = "DataScythe Local Code Signing (2026)"
$publicCert = Join-Path $root "docs\DataScythe_Local_Code_Signing_2026.cer"
$privateDir = Join-Path $root ".gnupg-release\private-keys-v1.d"
$pfxPath = Join-Path $privateDir "DataScythe_Local_Code_Signing_2026.pfx"
$p12Path = Join-Path $privateDir "DataScythe_Local_Code_Signing_2026.p12"
$passwordFile = Join-Path $root ".gnupg-release\code-signing-password.txt"

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
    -NotAfter (Get-Date).AddYears(5)

$password = -join ((48..57 + 65..90 + 97..122) | Get-Random -Count 32 | ForEach-Object { [char]$_ })
$securePassword = ConvertTo-SecureString -String $password -Force -AsPlainText

Export-Certificate -Cert $cert -FilePath $publicCert | Out-Null
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $securePassword | Out-Null
Copy-Item -LiteralPath $pfxPath -Destination $p12Path -Force

@(
    "DataScythe local code-signing export password."
    "Store location: .gnupg-release/private-keys-v1.d/"
    "Public certificate: docs/DataScythe_Local_Code_Signing_2026.cer"
    ""
    "Password:"
    $password
) | Set-Content -LiteralPath $passwordFile -Encoding UTF8

& (Join-Path $PSScriptRoot "hide_gnupg_release.ps1") | Out-Null

Write-Host "[PASS] Code-signing certificate created"
Write-Host "Public:  docs/DataScythe_Local_Code_Signing_2026.cer"
Write-Host "Private: .gnupg-release/private-keys-v1.d/DataScythe_Local_Code_Signing_2026.pfx"
Write-Host "Private: .gnupg-release/private-keys-v1.d/DataScythe_Local_Code_Signing_2026.p12"
Write-Host "Password file (gitignored): .gnupg-release/code-signing-password.txt"
Write-Host "Thumbprint: $($cert.Thumbprint)"