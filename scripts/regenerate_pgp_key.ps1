$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

$gpgHome = ".gnupg-release"
$gpg = "C:\Program Files\Git\usr\bin\gpg.exe"
if (-not (Test-Path -LiteralPath $gpg)) {
    $gpg = (Get-Command gpg -ErrorAction SilentlyContinue).Source
}
if (-not $gpg) {
    throw "gpg not found."
}

$privateDir = Join-Path $gpgHome "private-keys-v1.d"
New-Item -ItemType Directory -Force -Path $privateDir | Out-Null

Write-Host "[RUN] Removing existing PGP key material (preserving code-signing PFX/P12)"
foreach ($pattern in @("*.key", "pubring.kbx", "pubring.kbx~", "trustdb.gpg")) {
    Get-ChildItem -LiteralPath $gpgHome -Filter $pattern -ErrorAction SilentlyContinue |
        Remove-Item -Force
}
if (Test-Path -LiteralPath "$gpgHome/openpgp-revocs.d") {
    Remove-Item -LiteralPath "$gpgHome/openpgp-revocs.d" -Recurse -Force
}

Write-Host "[RUN] Generating single-key PGP release signing key"
$null = & $gpg --homedir $gpgHome --batch --generate-key "scripts/gpg-keygen-batch.txt" 2>&1

Write-Host "[RUN] Exporting public key"
$null = & $gpg --homedir $gpgHome --batch --pinentry-mode loopback --yes --armor --export `
    --output "docs/DataScythe_Release_Signing_2026_pubkey.asc" "release@datascythe.local" 2>&1

$keyInfo = (& $gpg --homedir $gpgHome --list-secret-keys --keyid-format long 2>&1) -join "`n"
if ($keyInfo -match "ssb") {
    throw "Expected a single primary key only, but a subkey was created."
}

$keyFiles = Get-ChildItem -LiteralPath $privateDir -Filter "*.key" -ErrorAction SilentlyContinue
if ($keyFiles.Count -ne 1) {
    throw "Expected exactly one .key file, found $($keyFiles.Count)."
}

& (Join-Path $PSScriptRoot "hide_gnupg_release.ps1") | Out-Null

Write-Host "[PASS] Single-key PGP identity ready"
Write-Host $keyInfo
Write-Host "Key files:"
$keyFiles | ForEach-Object { Write-Host "  $($_.FullName)" }