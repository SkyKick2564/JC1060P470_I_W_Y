$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$VersionPath = Join-Path $ProjectRoot "version.txt"
$PendingPath = Join-Path $ProjectRoot ".version-build.pending"

if (-not (Test-Path $VersionPath -PathType Leaf)) {
    throw "Version file not found: $VersionPath"
}

# A pending marker means the previous build did not reach its post-build hook.
# Restore its original version so a failed build does not consume a version.
if (Test-Path $PendingPath -PathType Leaf) {
    $PreviousVersion = (Get-Content $PendingPath -Raw).Trim()
    [System.IO.File]::WriteAllText(
        $VersionPath,
        $PreviousVersion + "`n",
        [System.Text.UTF8Encoding]::new($false)
    )
    Remove-Item $PendingPath -Force
    Write-Host "Recovered version after an interrupted/failed build: $PreviousVersion"
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

# Save the old version until the successful post-build hook runs.
[System.IO.File]::WriteAllText($PendingPath, $CurrentVersion + "`n", $Utf8NoBom)
[System.IO.File]::WriteAllText($VersionPath, $NewVersion + "`n", $Utf8NoBom)

Write-Host ""
Write-Host "Firmware version: $CurrentVersion -> $NewVersion"
Write-Host ""
