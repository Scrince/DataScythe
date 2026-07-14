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

function Invoke-Gpg([string[]]$Arguments) {
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $gpg @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "gpg failed: gpg $($Arguments -join ' ')"
    }
}

$privateDir = Join-Path $gpgHome "private-keys-v1.d"
New-Item -ItemType Directory -Force -Path $privateDir | Out-Null

Write-Host "[RUN] Removing existing PGP key material (preserving code-signing PFX/P12)"
foreach ($pattern in @("*.key", "pubring.kbx", "pubring.kbx~", "trustdb.gpg")) {
    Get-ChildItem -LiteralPath $gpgHome -Filter $pattern -ErrorAction SilentlyContinue |
        Remove-Item -Force
}
Get-ChildItem -LiteralPath $privateDir -Filter "*.key" -ErrorAction SilentlyContinue |
    Remove-Item -Force
if (Test-Path -LiteralPath "$gpgHome/openpgp-revocs.d") {
    Remove-Item -LiteralPath "$gpgHome/openpgp-revocs.d" -Recurse -Force
}

Write-Host "[RUN] Restoring primary key plus signing/encryption subkeys"
foreach ($backup in @("PrivateKey.txt", "SubKey.txt", "EncryptionKey.txt")) {
    $backupPath = Join-Path $privateDir $backup
    if (-not (Test-Path -LiteralPath $backupPath)) {
        throw "Missing armored key backup: $backupPath"
    }
}
Invoke-Gpg @("--homedir", $gpgHome, "--batch", "--import",
             (Join-Path $privateDir "PrivateKey.txt"),
             (Join-Path $privateDir "SubKey.txt"),
             (Join-Path $privateDir "EncryptionKey.txt"))

Write-Host "[RUN] Exporting public key"
Invoke-Gpg @("--homedir", $gpgHome, "--batch", "--pinentry-mode", "loopback", "--yes",
             "--armor", "--export", "--output",
             "docs/DataScythe_Release_Signing_2026_pubkey.asc", "release@datascythe.local")

$oldErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $keyInfo = (& $gpg --homedir $gpgHome --list-secret-keys --keyid-format long 2>&1) -join "`n"
    $keyListExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $oldErrorActionPreference
}
if ($keyListExitCode -ne 0) {
    throw "Unable to list regenerated PGP key."
}
if ($keyInfo -notmatch "sec\s+rsa4096/.+\[C\]") {
    throw "Expected a primary certification key."
}
if ($keyInfo -notmatch "ssb\s+ed25519/.+\[S\]") {
    throw "Expected an Ed25519 signing subkey."
}
if ($keyInfo -notmatch "ssb\s+cv25519/.+\[E\]") {
    throw "Expected a Cv25519 encryption subkey."
}

$keyFiles = Get-ChildItem -LiteralPath $privateDir -Filter "*.key" -ErrorAction SilentlyContinue
if ($keyFiles.Count -ne 3) {
    throw "Expected exactly three .key files, found $($keyFiles.Count)."
}

& (Join-Path $PSScriptRoot "hide_gnupg_release.ps1") | Out-Null

Write-Host "[PASS] PGP primary/subkey identity ready"
Write-Host $keyInfo
Write-Host "Key files:"
$keyFiles | ForEach-Object { Write-Host "  $($_.FullName)" }
