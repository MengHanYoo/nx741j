param(
    [Parameter(Mandatory = $true)] [string] $Image,
    [Parameter(Mandatory = $true)] [string] $ExtentsJson,
    [string] $Adb = 'E:\default-workspace\tools\platform-tools\adb.exe',
    [string] $RemoteImage = '/data/local/tmp/iosroot.img',
    [string] $StageImage = '/data/local/tmp/iosroot-source-20261122.img',
    [string] $Userdata = '/dev/block/sda12',
    [switch] $Write
)

$ErrorActionPreference = 'Stop'
$pinfile = '/data/local/tmp/pinfile'
$expectedSize = 17179869184

function Invoke-AdbText([string] $Command) {
    $value = & $Adb shell $Command
    if ($LASTEXITCODE -ne 0) { throw "adb shell failed: $Command" }
    return (($value -join "`n").Trim())
}

if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) { throw "image not found: $Image" }
if (-not (Test-Path -LiteralPath $ExtentsJson -PathType Leaf)) { throw "extent table not found: $ExtentsJson" }
if (-not (Test-Path -LiteralPath $Adb -PathType Leaf)) { throw "adb not found: $Adb" }
if ((Get-Item -LiteralPath $Image).Length -ne $expectedSize) { throw "image is not 16 GiB" }

$devices = & $Adb devices
$online = @($devices | Where-Object { $_ -match '^\S+\s+device$' })
if ($online.Count -ne 1) { throw "exactly one ADB device is required; found $($online.Count)" }
if ((Invoke-AdbText 'id -u') -ne '0') { throw 'adb shell is not root' }

$status = Invoke-AdbText "$pinfile status $RemoteImage"
if ($status -notmatch 'pinned \(GC move failures 0\)' -or
    $status -notmatch "size $expectedSize bytes") {
    throw "remote image is not the expected pinned 16 GiB file: $status"
}

$saved = Get-Content -Raw -Encoding utf8 -LiteralPath $ExtentsJson | ConvertFrom-Json
$currentText = Invoke-AdbText "$pinfile extents $RemoteImage --json"
$current = $currentText | ConvertFrom-Json
if ([int64]$saved.size -ne $expectedSize -or [int64]$current.size -ne $expectedSize) {
    throw 'extent JSON size is not 16 GiB'
}
if ($saved.file -ne $RemoteImage -or $current.file -ne $RemoteImage) {
    throw "extent JSON path does not name $RemoteImage"
}
$savedExt = $saved.extents | ConvertTo-Json -Compress -Depth 4
$currentExt = $current.extents | ConvertTo-Json -Compress -Depth 4
if ($savedExt -ne $currentExt) { throw 'F2FS extents changed; refusing to write' }
Write-Host "PASS  pinned 16 GiB file and $($saved.extents.Count) extents match"

$firstRead = Invoke-AdbText "$pinfile rawreadat $RemoteImage $Userdata 0 4096 | sha256sum"
$lastOffset = $expectedSize - 4096
$lastRead = Invoke-AdbText "$pinfile rawreadat $RemoteImage $Userdata $lastOffset 4096 | sha256sum"
Write-Host "raw first block baseline: $firstRead"
Write-Host "raw last block baseline:  $lastRead"
Write-Host 'PASS  first and last extent blocks are readable through raw userdata'

$imageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Image).Hash.ToLowerInvariant()
Write-Host "image SHA-256: $imageHash"
if (-not $Write) {
    Write-Host 'Read-only preflight passed. Re-run with -Write to stream the image into the pinned extents.'
    exit 0
}

Write-Host "Uploading image to temporary device input: $StageImage"
& $Adb push $Image $StageImage
if ($LASTEXITCODE -ne 0) { throw "adb push failed with exit code $LASTEXITCODE" }
$stageHash = Invoke-AdbText "sha256sum $StageImage"
if ($stageHash -notmatch "(?m)^$imageHash\b") {
    throw "uploaded staging hash mismatch: image=$imageHash device=$stageHash"
}
Write-Host 'PASS  staged input hash matches the published image'

Write-Host "Writing $expectedSize bytes through pinfile -> $Userdata"
$rawOutput = Invoke-AdbText "$pinfile rawwrite $RemoteImage $Userdata all --yes < $StageImage"
if ($rawOutput) { Write-Host $rawOutput }

$rawHash = Invoke-AdbText "$pinfile rawhash $RemoteImage $Userdata $expectedSize"
if ($rawHash -notmatch "(?m)^$imageHash\b") {
    throw "raw hash mismatch: image=$imageHash device=$rawHash"
}
$signature = Invoke-AdbText "$pinfile rawreadat $RemoteImage $Userdata 0 4096 | dd bs=1 skip=1024 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n'"
if ($signature -ne '4858') { throw "post-write HFSX signature missing: '$signature'" }
Write-Host 'PASS  raw 16 GiB hash and HFSX signature match the published image'
Invoke-AdbText "rm -f $StageImage" | Out-Null
