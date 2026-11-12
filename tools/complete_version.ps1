$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$VersionPath = Join-Path $ProjectRoot "version.txt"
$PendingPath = Join-Path $ProjectRoot ".version-build.pending"

if (-not (Test-Path $PendingPath -PathType Leaf)) {
    throw "Build version marker not found: $PendingPath"
}

$Version = (Get-Content $VersionPath -Raw).Trim()
Remove-Item $PendingPath -Force

Write-Host ""
Write-Host "Build completed successfully. Firmware version: $Version"
Write-Host ""
