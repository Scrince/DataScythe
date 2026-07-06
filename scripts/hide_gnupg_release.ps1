$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dir = Join-Path $root ".gnupg-release"

if (-not (Test-Path -LiteralPath $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

attrib +H $dir
Write-Host "Hidden attribute set on .gnupg-release"