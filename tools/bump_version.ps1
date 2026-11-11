$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$VersionPath = Join-Path $ProjectRoot "version.txt"

if (-not (Test-Path $VersionPath -PathType Leaf)) {
    throw "Version file not found: $VersionPath"
}

$CurrentVersion = (Get-Content $VersionPath -Raw).Trim()
if ($CurrentVersion -notmatch '^(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?$') {
    throw "Invalid version in version.txt: '$CurrentVersion'. Expected 3 or 4 numeric parts."
}

$Parts = @(
    [long]$Matches[1],
    [long]$Matches[2],
    [long]$Matches[3]
)

if ($Matches[4] -ne $null -and $Matches[4] -ne "") {
    $Parts += [long]$Matches[4]
}

$LastIndex = $Parts.Count - 1
if ($Parts[$LastIndex] -eq [long]::MaxValue) {
    throw "Version component overflow: $CurrentVersion"
}
$Parts[$LastIndex]++

$NewVersion = $Parts -join "."
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($VersionPath, $NewVersion + "`n", $Utf8NoBom)

Write-Host ""
Write-Host "Firmware version: $CurrentVersion -> $NewVersion"
Write-Host ""
