$ErrorActionPreference = "Stop"

$root = (Split-Path -Parent $PSScriptRoot)
Set-Location -LiteralPath $root

$gpgHome = ".gnupg-release"
$gpg = "C:\Program Files\Git\usr\bin\gpg.exe"
if (-not (Test-Path -LiteralPath $gpg)) {
    $gpg = (Get-Command gpg -ErrorAction SilentlyContinue).Source
}
if (-not $gpg) {
    throw "gpg not found. Install Git for Windows or GnuPG."
}

function Invoke-Gpg([string[]]$Arguments) {
    $env:GNUPGHOME = (Resolve-Path -LiteralPath $gpgHome).Path
    & $gpg @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "gpg failed: gpg $($Arguments -join ' ')"
    }
}

function Ensure-ReleaseKey {
    if (-not (Test-Path -LiteralPath $gpgHome)) {
        New-Item -ItemType Directory -Force -Path $gpgHome | Out-Null
    }
    & (Join-Path $PSScriptRoot "hide_gnupg_release.ps1") | Out-Null

    $keyList = & $gpg --homedir $gpgHome --list-secret-keys --keyid-format long 2>&1 | Out-String
    if ($keyList -notmatch "DataScythe Release Signing") {
        Write-Host "[RUN] Generating DataScythe release signing key in .gnupg-release"
        Invoke-Gpg @("--homedir", $gpgHome, "--batch", "--generate-key", "scripts/gpg-keygen-batch.txt")
    }

    Write-Host "[RUN] Exporting public key to docs/DataScythe_Release_Signing_2026_pubkey.asc"
    Invoke-Gpg @("--homedir", $gpgHome, "--batch", "--pinentry-mode", "loopback", "--yes",
                 "--armor", "--export", "--output", "docs/DataScythe_Release_Signing_2026_pubkey.asc",
                 "release@datascythe.local")
}

function Sign-Artifact([string]$RelativePath) {
    if (-not (Test-Path -LiteralPath $RelativePath)) {
        Write-Warning "Skipping missing artifact: $RelativePath"
        return
    }
    $signature = "$RelativePath.asc"
    if (Test-Path -LiteralPath $signature) {
        Remove-Item -LiteralPath $signature -Force
    }
    Write-Host "[RUN] Signing $RelativePath"
    Invoke-Gpg @("--homedir", $gpgHome, "--batch", "--pinentry-mode", "loopback", "--yes",
                 "--armor", "--detach-sign", "--local-user", "release@datascythe.local",
                 "--output", $signature, $RelativePath)
}

Ensure-ReleaseKey

if (-not (Test-Path -LiteralPath "docs/SHA256SUMS")) {
    throw "Missing docs/SHA256SUMS. Run scripts/generate_sha256sums.py first."
}

Sign-Artifact "docs/SHA256SUMS"

$releaseRoot = "releases/native"
if (Test-Path -LiteralPath $releaseRoot) {
    Get-ChildItem -LiteralPath $releaseRoot -Recurse -File |
        Where-Object {
            $_.Extension -ne ".asc" -and $_.Name -ne ".gitkeep" -and -not $_.Name.StartsWith("._")
        } |
        ForEach-Object {
            $relative = $_.FullName.Substring($root.Length + 1) -replace "\\", "/"
            Sign-Artifact $relative
        }
}

Write-Host "[PASS] Release signatures created."
Write-Host "Private key home: .gnupg-release/ (gitignored)"
Write-Host "Public key: docs/DataScythe_Release_Signing_2026_pubkey.asc"
$fingerprint = & $gpg --homedir $gpgHome --fingerprint --with-colons release@datascythe.local 2>$null |
    Where-Object { $_ -like "fpr:*" } | Select-Object -First 1
if ($fingerprint) {
    $fp = ($fingerprint -split ":")[9]
    $formatted = ($fp -replace "(.{4})", '$1 ').Trim().ToUpper()
    Write-Host "Fingerprint: $formatted"
}
Write-Host "Verify:"
Write-Host "  gpg --import docs/DataScythe_Release_Signing_2026_pubkey.asc"
Write-Host "  gpg --verify docs/SHA256SUMS.asc docs/SHA256SUMS"
