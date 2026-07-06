[CmdletBinding()]
param(
    [switch]$ValidateArtifacts
)

$ErrorActionPreference = "Stop"
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$version = "0.1.0"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-File([string]$Path, [long]$MinimumBytes = 1) {
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "Missing required file: $Path"
    $item = Get-Item -LiteralPath $Path
    Assert-True ($item.Length -ge $MinimumBytes) "File is unexpectedly small: $Path ($($item.Length) bytes)"
}

Write-Host "[RUN] DataScythe Windows release preflight"
Assert-File (Join-Path $root "CMakeLists.txt")
Assert-File (Join-Path $root "src\core\version.h")

$cli = Join-Path $root "build\apps\cli\datascythe.exe"
if (Test-Path -LiteralPath $cli) {
    Assert-File $cli 100KB
    & $cli --version
    if ($LASTEXITCODE -ne 0) { throw "datascythe --version failed" }
}

if ($ValidateArtifacts) {
    $artifactRoot = Join-Path $root "releases\native\windows"
    $zip = Get-ChildItem -LiteralPath $artifactRoot -Filter "DataScythe-$version-win64.zip" -File -ErrorAction SilentlyContinue
    Assert-True ($zip.Count -gt 0) "No staged Windows ZIP found under releases/native/windows"
    foreach ($item in $zip) {
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        Write-Host "[PASS] $($item.Name) SHA256=$hash"
    }
}

Write-Host "[PASS] Windows release preflight complete"