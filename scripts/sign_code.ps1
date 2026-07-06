param(
    [Parameter(Mandatory = $true)]
    [string]$TargetPath
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$pfxPath = Join-Path $root ".gnupg-release\private-keys-v1.d\DataScythe_Local_Code_Signing_2026.pfx"
$passwordFile = Join-Path $root ".gnupg-release\code-signing-password.txt"

if (-not (Test-Path -LiteralPath $pfxPath)) {
    throw "Missing PFX. Run scripts/generate_code_signing_cert.ps1 first."
}
if (-not (Test-Path -LiteralPath $passwordFile)) {
    throw "Missing password file: .gnupg-release/code-signing-password.txt"
}
if (-not (Test-Path -LiteralPath $TargetPath)) {
    throw "Target not found: $TargetPath"
}

$password = Get-Content -LiteralPath $passwordFile | Select-Object -Last 1
$securePassword = ConvertTo-SecureString -String $password -Force -AsPlainText
$pfxData = Get-PfxData -FilePath $pfxPath -Password $securePassword
$cert = $pfxData.EndEntityCertificates[0]

$result = Set-AuthenticodeSignature -FilePath $TargetPath -Certificate $cert -TimestampServer "http://timestamp.digicert.com"
if ($result.Status -ne "Valid" -and $result.Status -ne "UnknownError") {
    Write-Warning "Authenticode status: $($result.Status)"
}
Write-Host "[PASS] Signed $TargetPath"
Write-Host "Status: $($result.Status)"