$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$version = "0.1.0"
$buildRoot = Join-Path $root "build"
$releaseRoot = Join-Path $root "releases\native\windows"
$stageName = "DataScythe-$version-win64"
$stageDir = Join-Path $releaseRoot $stageName
$guiBuild = Join-Path $buildRoot "apps\gui"
$cliBuild = Join-Path $buildRoot "apps\cli"

function Copy-IfExists([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

if (-not (Test-Path -LiteralPath $buildRoot)) {
    throw "Build directory not found. Run cmake --build build first."
}

if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$runtimeExtensions = @(".exe", ".dll")
if (Test-Path -LiteralPath (Join-Path $guiBuild "DataScythe.exe")) {
    Get-ChildItem -LiteralPath $guiBuild -File | Where-Object {
        $runtimeExtensions -contains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stageDir $_.Name) -Force
    }
    foreach ($subdir in @("platforms", "styles", "imageformats", "iconengines", "tls", "translations", "generic")) {
        $source = Join-Path $guiBuild $subdir
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $stageDir $subdir) -Recurse -Force
        }
    }
} else {
    Write-Warning "GUI binary not found; staging CLI only."
}

# Windows file systems are case-insensitive; use datascythe-cli.exe to avoid
# colliding with DataScythe.exe in the same release folder.
Copy-IfExists (Join-Path $cliBuild "datascythe.exe") (Join-Path $stageDir "datascythe-cli.exe")

@"
DataScythe v$version (Windows x64)

Binaries:
  DataScythe.exe      Qt GUI (run as Administrator for drive operations)
  datascythe-cli.exe  Command-line interface

Verify against docs/SHA256SUMS and detached PGP signatures (*.asc) before use.
"@ | Set-Content -LiteralPath (Join-Path $stageDir "README.txt") -Encoding UTF8

$zipPath = Join-Path $releaseRoot "$stageName.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath

Write-Host "Staged Windows release assets:"
Write-Host "  $stageDir"
Write-Host "  $zipPath"
Write-Host "Next: python scripts/generate_sha256sums.py"
Write-Host "Then: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/sign_release_assets.ps1"