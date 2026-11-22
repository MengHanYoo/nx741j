#requires -Version 5.1
<#
  RAM-only boot using the data-a-live flow. This script has no flash command
  and refuses images other than the pinned NX741J proven boot image.
#>
param(
    [string]$Image = 'E:\default-workspace\z80u-collect\repos\nx741j\shim\loader\out-proven-gpu\loader-boot.img',
    [string]$ExpectedSha256 = '1417864756da79f212856ef20debe855ced8cca6beec431d243d343f10624398',
    [int]$TimeoutSec = 120
)

$ErrorActionPreference = 'Stop'
$Fastboot = 'E:\default-workspace\tools\platform-tools\fastboot.exe'
$OutDir = 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route\fastboot-boot'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Report = Join-Path $OutDir ($Stamp + '.json')
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

function Log([string]$Message) {
    $line = '[' + (Get-Date -Format 'HH:mm:ss') + '] ' + $Message
    Write-Output $line
}

function Fail([string]$Message) {
    Log ('FAILED: ' + $Message)
    throw $Message
}

if (-not (Test-Path -LiteralPath $Image)) { Fail ('missing image: ' + $Image) }
$sha = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sha -ne $ExpectedSha256) {
    Fail ("image sha256 {0} != {1}" -f $sha, $ExpectedSha256)
}
$size = (Get-Item -LiteralPath $Image).Length
Log ("RAM-only candidate: {0} bytes sha256 {1}" -f $size, $sha)

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$devices = @()
while ((Get-Date) -lt $deadline) {
    $devices = @(& $Fastboot devices 2>&1 | Where-Object { $_ -match '\S' -and $_ -notmatch '^< waiting' })
    if ($devices.Count -eq 1) { break }
    if ($devices.Count -gt 1) { Fail 'more than one fastboot device' }
    Start-Sleep -Seconds 2
}
if ($devices.Count -ne 1) { Fail ('no fastboot device after {0}s' -f $TimeoutSec) }
Log ('fastboot device: ' + $devices[0])

# Read-only identity query.
$product = (& $Fastboot getvar product 2>&1) -join "`n"
Log ('product query: ' + (($product -split "`n" | Where-Object { $_.Trim() }) -join ' | '))

Log 'executing fastboot boot (RAM only; no partition write)'
$output = (& $Fastboot boot $Image 2>&1) -join "`n"
if ($output -notmatch 'Booting.*OKAY|Booting.*FINISHED') {
    Fail ("fastboot boot did not report OKAY`n" + $output)
}
$result = [ordered]@{
    timestamp = $Stamp
    mode = 'fastboot-boot-ram-only'
    image = $Image
    image_bytes = $size
    image_sha256 = $sha
    product = $product
    output = $output
    partition_write = $false
    status = 'booting'
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Report -Encoding UTF8
Log ('fastboot boot OK; report ' + $Report)
