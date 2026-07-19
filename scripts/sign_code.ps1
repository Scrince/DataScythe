param(
    [Parameter(Mandatory = $true)]
    [string]$TargetPath
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$pfxPath = Join-Path $root ".gnupg-release\private-keys-v1.d\DataScythe_Local_Code_Signing_2026.pfx"

if (-not (Test-Path -LiteralPath $pfxPath)) {
    throw "Missing PFX. Run scripts/generate_code_signing_cert.ps1 first."
}
if (-not (Test-Path -LiteralPath $TargetPath)) {
    throw "Target not found: $TargetPath"
}

# Empty password (matches suite-wide local code-signing exports)
$securePassword = [securestring]::new()
$pfxData = Get-PfxData -FilePath $pfxPath -Password $securePassword
$cert = $pfxData.EndEntityCertificates[0]

$result = Set-AuthenticodeSignature -FilePath $TargetPath -Certificate $cert -TimestampServer "http://timestamp.digicert.com"
if ($result.Status -ne "Valid" -and $result.Status -ne "UnknownError") {
    Write-Warning "Authenticode status: $($result.Status)"
}
Write-Host "[PASS] Signed $TargetPath"
Write-Host "Status: $($result.Status)"
