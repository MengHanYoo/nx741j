#requires -Version 5.1
<#
  Write the two rawdump regions this device boots from (efisp route):

    payload.img  -> LUN0 sector 4743976, 64 MiB
    firmware FD  -> LUN0 sector 4760360, 4 MiB  (4743976 + 16384)

  Both go through wsl\flash-boota-9008.ps1 one after the other in the same
  9008 session: Sahara and <configure> are sent once, the second write skips
  them. boot_a is NOT touched - ABL rejects our boot.img on this machine.

  Verification is on by default: the inner script reads every region back and
  compares the SHA-256 it staged.
#>
param(
    [Parameter(Mandatory = $true)][string]$Payload,
    [string]$Fd,
    [string]$ExpectPayloadSha = '8801fbd728c35227c193d2aac8dfcf1302ea02a5b2f47c55b07c19f9ab0de95c',
    [string]$ExpectFdSha = '3ce5c6d2c61891975c52846358bff3f3d9744413d61fd732ea48f7507a49a009',
    [switch]$DryRun,
    [switch]$NoReboot,
    [switch]$SkipSahara
)

$ErrorActionPreference = 'Stop'
$Inner = Join-Path $PSScriptRoot 'flash-boota-9008.ps1'
$RawdumpLba = 4743976
$RawdumpSectors = 16384
$RawdumpBytes = 512MB
$FdLba = $RawdumpLba + $RawdumpSectors

function Sha([string]$path) { (Get-FileHash $path -Algorithm SHA256).Hash.ToLower() }
function Fail([string]$m) { Write-Host "FAIL: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Payload)) { Fail "payload not found: $Payload" }
$payloadSha = Sha $Payload
$payloadBytes = (Get-Item $Payload).Length
Write-Host ("payload {0}  {1} B  sha256 {2}" -f (Split-Path $Payload -Leaf), $payloadBytes, $payloadSha)
if ($payloadBytes -ne $RawdumpSectors * 4096) { Fail 'payload must be exactly 64 MiB for this layout' }
if ($ExpectPayloadSha -and $payloadSha -ne $ExpectPayloadSha) { Fail 'payload sha256 does not match the expected artifact' }

if ($Fd) {
    if (-not (Test-Path $Fd)) { Fail "firmware not found: $Fd" }
    $fdSha = Sha $Fd
    $fdBytes = (Get-Item $Fd).Length
    Write-Host ("firmware {0}  {1} B  sha256 {2}" -f (Split-Path $Fd -Leaf), $fdBytes, $fdSha)
    if ($fdBytes -ne 4MB) { Fail 'firmware volume must be exactly 4 MiB' }
    if ($ExpectFdSha -and $fdSha -ne $ExpectFdSha) { Fail 'firmware sha256 does not match the expected artifact' }
}

Write-Host ''
Write-Host "step 1/2  payload -> rawdump+0        (LUN0 sector $RawdumpLba)"
$first = @{ Label = 'rawdump'; Lun = 0; PartitionBytes = $RawdumpBytes; StartSector = $RawdumpLba }
if ($DryRun) { $first.DryRun = $true }
if ($SkipSahara) { $first.SkipSahara = $true }
# keep the device in EDL when a second region follows, otherwise the reset ends
# the session and the firmware write would have no programmer loaded
if ($NoReboot -or $Fd) { $first.NoReboot = $true }
& $Inner -Img $Payload @first
$code = $LASTEXITCODE
if ($null -ne $code -and $code -ne 0) { Fail "payload write failed (exit $code)" }
Write-Host "payload written and verified"

if ($Fd) {
    Write-Host ''
    Write-Host "step 2/2  firmware -> rawdump+64 MiB  (LUN0 sector $FdLba)  [same 9008 session]"
    $second = @{ Label = 'rawdump'; Lun = 0; PartitionBytes = $RawdumpBytes; StartSector = $FdLba; SkipSahara = $true }
    if (-not $SkipSahara) { $second.SkipConfigure = $true }
    if ($DryRun) { $second.DryRun = $true }
    if ($NoReboot) { $second.NoReboot = $true }
    & $Inner -Img $Fd @second
    $code = $LASTEXITCODE
    if ($null -ne $code -and $code -ne 0) { Fail "firmware write failed (exit $code)" }
    Write-Host "firmware written and verified"
}

Write-Host ''
Write-Host 'done. Reboot the device: it should come back into the UniXNU menu.' -ForegroundColor Green
