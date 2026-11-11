# Publish the current ESP-IDF application binary to GitHub Releases
# and update the public OTA manifest in the current Git branch.
$ErrorActionPreference = "Stop"

$Repository = "SkyKick2564/JC1060P470_I_W_Y"
$ExpectedBranch = "network-manager-freertos-task"
$ProjectName = "JC1060P470_I_W_Y"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$FirmwarePath = Join-Path $ProjectRoot "build\\$ProjectName.bin"
$VersionPath = Join-Path $ProjectRoot "version.txt"
$ManifestPath = Join-Path $ProjectRoot "ota\\manifest.json"

function Invoke-Git {
    param([string[]]$GitArgs)
    & git -C $ProjectRoot @GitArgs
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed: git $($GitArgs -join ' ')"
    }
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI (gh) is not installed. Install it from https://cli.github.com/ and reopen PowerShell."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is not installed or is not available in PATH."
}
if (-not (Test-Path $FirmwarePath -PathType Leaf)) {
    throw "Firmware binary not found: $FirmwarePath. Build the project first."
}
if (-not (Test-Path $VersionPath -PathType Leaf)) {
    throw "Version file not found: $VersionPath"
}
if (Test-Path (Join-Path $ProjectRoot ".version-build.pending") -PathType Leaf) {
    throw "The latest build is pending or failed. Run ESP-IDF: Build your Project successfully before publishing."
}

$Branch = (& git -C $ProjectRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or $Branch -ne $ExpectedBranch) {
    throw "Run this script on branch '$ExpectedBranch'. Current branch: '$Branch'"
}

# The firmware must correspond to committed source and version.txt.
$WorkingTreeStatus = @(& git -C $ProjectRoot status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect the Git working tree."
}
if ($WorkingTreeStatus.Count -gt 0) {
    Write-Host "The working tree contains uncommitted or untracked files:" -ForegroundColor Yellow
    $WorkingTreeStatus | ForEach-Object { Write-Host "  $_" }
    throw "Commit or discard these changes, then run the publish script again."
}

& git -C $ProjectRoot ls-files --error-unmatch -- version.txt *> $null
if ($LASTEXITCODE -ne 0) {
    throw "version.txt is not tracked by Git. Add and commit it before publishing."
}

# Fetch first, then require the local branch to match the remote exactly.
Invoke-Git -GitArgs @("fetch", "origin", $Branch)
$RemoteBranch = "refs/remotes/origin/$Branch"
& git -C $ProjectRoot rev-parse --verify $RemoteBranch *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Remote branch origin/$Branch was not found."
}

$LocalCommit = (& git -C $ProjectRoot rev-parse HEAD).Trim()
$RemoteCommit = (& git -C $ProjectRoot rev-parse $RemoteBranch).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not compare the local and remote commits."
}
if ($LocalCommit -ne $RemoteCommit) {
    throw "Local branch '$Branch' is not synchronized with origin/$Branch. Push or pull the required commits, rebuild if the source changed, then publish again."
}

$Version = (Get-Content $VersionPath -Raw).Trim()
if ($Version -notmatch '^\d+(\.\d+){1,3}$') {
    throw "Invalid version in version.txt: '$Version'. Use 2-4 numeric parts, e.g. 1.0.2"
}

& gh auth status --hostname github.com
if ($LASTEXITCODE -ne 0) {
    throw "GitHub CLI is not authenticated. Run: gh auth login"
}

$Tag = "v$Version"
$FirmwareName = "$ProjectName.bin"
$FirmwareSize = (Get-Item $FirmwarePath).Length
$FirmwareSha256 = (Get-FileHash $FirmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
$FirmwareUrl = "https://github.com/$Repository/releases/download/$Tag/$FirmwareName"

Write-Host ""
Write-Host "Project : $ProjectName"
Write-Host "Version : $Version"
Write-Host "Binary  : $FirmwarePath"
Write-Host "Size    : $FirmwareSize bytes"
Write-Host "SHA-256 : $FirmwareSha256"
Write-Host "Git     : clean and synchronized with origin/$Branch"
Write-Host ""

$PreviousErrorActionPreference = $ErrorActionPreference
try {
    # A missing release is expected on first publish. Prevent native gh.exe
    # stderr from becoming a terminating PowerShell error.
    $ErrorActionPreference = "Continue"
    & gh release view $Tag --repo $Repository *> $null
    $ReleaseViewExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $PreviousErrorActionPreference
}

if ($ReleaseViewExitCode -eq 0) {
    Write-Host "Release $Tag exists; uploading/replacing firmware asset..."
    & gh release upload $Tag $FirmwarePath --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not upload firmware asset to release $Tag"
    }
} else {
    Write-Host "Creating GitHub Release $Tag..."
    & gh release create $Tag $FirmwarePath --title "Firmware $Version" --notes "Firmware release for $ProjectName, version $Version." --target $Branch --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create GitHub Release $Tag"
    }
}

$Manifest = [ordered]@{
    project = $ProjectName
    version = $Version
    firmware_url = $FirmwareUrl
    size = $FirmwareSize
    sha256 = $FirmwareSha256
}
$ManifestJson = $Manifest | ConvertTo-Json
New-Item -ItemType Directory -Force -Path (Split-Path $ManifestPath) | Out-Null
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson + "`n", $Utf8NoBom)

Invoke-Git -GitArgs @("add", "--", "ota/manifest.json")
& git -C $ProjectRoot diff --cached --quiet -- ota/manifest.json
if ($LASTEXITCODE -ne 0) {
    Invoke-Git -GitArgs @("commit", "-m", "Publish OTA manifest for $Version")
    Invoke-Git -GitArgs @("push", "origin", $Branch)
} else {
    Write-Host "OTA manifest is already up to date; no manifest commit needed."
}

Write-Host ""
Write-Host "GitHub OTA release published successfully."
Write-Host "Manifest: https://raw.githubusercontent.com/$Repository/$Branch/ota/manifest.json"
Write-Host "Firmware: $FirmwareUrl"
) {
    throw "Invalid version in version.txt: '$Version'. Use 2-4 numeric parts, e.g. 1.0.2"
}

& gh auth status --hostname github.com
if ($LASTEXITCODE -ne 0) {
    throw "GitHub CLI is not authenticated. Run: gh auth login"
}

$Tag = "v$Version"
$FirmwareName = "$ProjectName.bin"
$FirmwareSize = (Get-Item $FirmwarePath).Length
$FirmwareSha256 = (Get-FileHash $FirmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
$FirmwareUrl = "https://github.com/$Repository/releases/download/$Tag/$FirmwareName"

Write-Host ""
Write-Host "Project : $ProjectName"
Write-Host "Version : $Version"
Write-Host "Binary  : $FirmwarePath"
Write-Host "Size    : $FirmwareSize bytes"
Write-Host "SHA-256 : $FirmwareSha256"
Write-Host "Git     : clean and synchronized with origin/$Branch"
Write-Host ""

$PreviousErrorActionPreference = $ErrorActionPreference
try {
    # A missing release is expected on first publish. Prevent native gh.exe
    # stderr from becoming a terminating PowerShell error.
    $ErrorActionPreference = "Continue"
    & gh release view $Tag --repo $Repository *> $null
    $ReleaseViewExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $PreviousErrorActionPreference
}

if ($ReleaseViewExitCode -eq 0) {
    Write-Host "Release $Tag exists; uploading/replacing firmware asset..."
    & gh release upload $Tag $FirmwarePath --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not upload firmware asset to release $Tag"
    }
} else {
    Write-Host "Creating GitHub Release $Tag..."
    & gh release create $Tag $FirmwarePath --title "Firmware $Version" --notes "Firmware release for $ProjectName, version $Version." --target $Branch --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create GitHub Release $Tag"
    }
}

$Manifest = [ordered]@{
    project = $ProjectName
    version = $Version
    firmware_url = $FirmwareUrl
    size = $FirmwareSize
    sha256 = $FirmwareSha256
}
$ManifestJson = $Manifest | ConvertTo-Json
New-Item -ItemType Directory -Force -Path (Split-Path $ManifestPath) | Out-Null
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson + "`n", $Utf8NoBom)

Invoke-Git -GitArgs @("add", "--", "ota/manifest.json")
& git -C $ProjectRoot diff --cached --quiet -- ota/manifest.json
if ($LASTEXITCODE -ne 0) {
    Invoke-Git -GitArgs @("commit", "-m", "Publish OTA manifest for $Version")
    Invoke-Git -GitArgs @("push", "origin", $Branch)
} else {
    Write-Host "OTA manifest is already up to date; no manifest commit needed."
}

Write-Host ""
Write-Host "GitHub OTA release published successfully."
Write-Host "Manifest: https://raw.githubusercontent.com/$Repository/$Branch/ota/manifest.json"
Write-Host "Firmware: $FirmwareUrl"
) {
    throw "Invalid version in version.txt: '$Version'. Use 2-4 numeric parts, e.g. 1.0.2"
}

& gh auth status --hostname github.com
if ($LASTEXITCODE -ne 0) {
    throw "GitHub CLI is not authenticated. Run: gh auth login"
}

$Tag = "v$Version"
$FirmwareName = "$ProjectName.bin"
$FirmwareSize = (Get-Item $FirmwarePath).Length
$FirmwareSha256 = (Get-FileHash $FirmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
$FirmwareUrl = "https://github.com/$Repository/releases/download/$Tag/$FirmwareName"

Write-Host ""
Write-Host "Project : $ProjectName"
Write-Host "Version : $Version"
Write-Host "Binary  : $FirmwarePath"
Write-Host "Size    : $FirmwareSize bytes"
Write-Host "SHA-256 : $FirmwareSha256"
Write-Host "Git     : clean and synchronized with origin/$Branch"
Write-Host ""

$PreviousErrorActionPreference = $ErrorActionPreference
try {
    # A missing release is expected on first publish. Prevent native gh.exe
    # stderr from becoming a terminating PowerShell error.
    $ErrorActionPreference = "Continue"
    & gh release view $Tag --repo $Repository *> $null
    $ReleaseViewExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $PreviousErrorActionPreference
}

if ($ReleaseViewExitCode -eq 0) {
    Write-Host "Release $Tag exists; uploading/replacing firmware asset..."
    & gh release upload $Tag $FirmwarePath --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not upload firmware asset to release $Tag"
    }
} else {
    Write-Host "Creating GitHub Release $Tag..."
    & gh release create $Tag $FirmwarePath --title "Firmware $Version" --notes "Firmware release for $ProjectName, version $Version." --target $Branch --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create GitHub Release $Tag"
    }
}

$Manifest = [ordered]@{
    project = $ProjectName
    version = $Version
    firmware_url = $FirmwareUrl
    size = $FirmwareSize
    sha256 = $FirmwareSha256
}
$ManifestJson = $Manifest | ConvertTo-Json
New-Item -ItemType Directory -Force -Path (Split-Path $ManifestPath) | Out-Null
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson + "`n", $Utf8NoBom)

Invoke-Git -GitArgs @("add", "--", "ota/manifest.json")
& git -C $ProjectRoot diff --cached --quiet -- ota/manifest.json
if ($LASTEXITCODE -ne 0) {
    Invoke-Git -GitArgs @("commit", "-m", "Publish OTA manifest for $Version")
    Invoke-Git -GitArgs @("push", "origin", $Branch)
} else {
    Write-Host "OTA manifest is already up to date; no manifest commit needed."
}

Write-Host ""
Write-Host "GitHub OTA release published successfully."
Write-Host "Manifest: https://raw.githubusercontent.com/$Repository/$Branch/ota/manifest.json"
Write-Host "Firmware: $FirmwareUrl"
) {
    throw "Invalid version in version.txt: '$Version'. Use 2-4 numeric parts, e.g. 1.0.2"
}

& gh auth status --hostname github.com
if ($LASTEXITCODE -ne 0) {
    throw "GitHub CLI is not authenticated. Run: gh auth login"
}

$Tag = "v$Version"
$FirmwareName = "$ProjectName.bin"
$FirmwareSize = (Get-Item $FirmwarePath).Length
$FirmwareSha256 = (Get-FileHash $FirmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
$FirmwareUrl = "https://github.com/$Repository/releases/download/$Tag/$FirmwareName"

Write-Host ""
Write-Host "Project : $ProjectName"
Write-Host "Version : $Version"
Write-Host "Binary  : $FirmwarePath"
Write-Host "Size    : $FirmwareSize bytes"
Write-Host "SHA-256 : $FirmwareSha256"
Write-Host "Git     : clean and synchronized with origin/$Branch"
Write-Host ""

$PreviousErrorActionPreference = $ErrorActionPreference
try {
    # A missing release is expected on first publish. Prevent native gh.exe
    # stderr from becoming a terminating PowerShell error.
    $ErrorActionPreference = "Continue"
    & gh release view $Tag --repo $Repository *> $null
    $ReleaseViewExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $PreviousErrorActionPreference
}

if ($ReleaseViewExitCode -eq 0) {
    Write-Host "Release $Tag exists; uploading/replacing firmware asset..."
    & gh release upload $Tag $FirmwarePath --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not upload firmware asset to release $Tag"
    }
} else {
    Write-Host "Creating GitHub Release $Tag..."
    & gh release create $Tag $FirmwarePath --title "Firmware $Version" --notes "Firmware release for $ProjectName, version $Version." --target $Branch --repo $Repository
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create GitHub Release $Tag"
    }
}

$Manifest = [ordered]@{
    project = $ProjectName
    version = $Version
    firmware_url = $FirmwareUrl
    size = $FirmwareSize
    sha256 = $FirmwareSha256
}
$ManifestJson = $Manifest | ConvertTo-Json
New-Item -ItemType Directory -Force -Path (Split-Path $ManifestPath) | Out-Null
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson + "`n", $Utf8NoBom)

Invoke-Git -GitArgs @("add", "--", "ota/manifest.json")
& git -C $ProjectRoot diff --cached --quiet -- ota/manifest.json
if ($LASTEXITCODE -ne 0) {
    Invoke-Git -GitArgs @("commit", "-m", "Publish OTA manifest for $Version")
    Invoke-Git -GitArgs @("push", "origin", $Branch)
} else {
    Write-Host "OTA manifest is already up to date; no manifest commit needed."
}

Write-Host ""
Write-Host "GitHub OTA release published successfully."
Write-Host "Manifest: https://raw.githubusercontent.com/$Repository/$Branch/ota/manifest.json"
Write-Host "Firmware: $FirmwareUrl"
