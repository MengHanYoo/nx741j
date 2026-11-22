#requires -Version 5.1
<#
  Restore the one 4 KiB sector that old efisp phase-7 loaders wrote when
  gRawBase was still zero. The bad record landed in super LBA 20224.

  The script is fail-closed:
    1. read the current sector over 9008;
    2. accept only a BP_RECORD ("DBD2", step 11) or the already-restored data;
    3. extract the same sector from the 2026-11-09 super backup;
    4. write and verify that sector only.

  It never rewrites the whole super partition.
#>
param(
    [string]$Backup = 'E:\Nubia_Flash_Brick\backup\ZTEToolBoxParBak_2026.11.09_14.05.38.363\images\super.img',
    [int]$Sector = 20224,
    [int]$SuperStart = 8968,
    [switch]$SkipSahara,
    [switch]$NoReboot,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$ReadScript = Join-Path $PSScriptRoot 'read-9008.ps1'
$WriteScript = Join-Path $PSScriptRoot 'flash-boota-9008.ps1'
$WorkRoot = 'E:\default-workspace\z80u-collect\_tmp\super-sector-repair'
$SectorSize = 4096

function Fail([string]$message) {
    Write-Host "FAIL: $message" -ForegroundColor Red
    exit 1
}

function Sha([string]$path) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()
}

if (-not (Test-Path -LiteralPath $Backup)) { Fail "super backup missing: $Backup" }
$backupLength = (Get-Item -LiteralPath $Backup).Length
$offset = ($Sector - $SuperStart) * $SectorSize
if ($offset -lt 0 -or $offset + $SectorSize -gt $backupLength) {
    Fail "sector $Sector is outside the super backup"
}

$work = Join-Path $WorkRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Path $work -Force | Out-Null
$restore = Join-Path $work 'super-lba-20224.bin'
$current = Join-Path $work 'super-lba-20224-current.bin'

$source = [IO.File]::OpenRead($Backup)
try {
    [void]$source.Seek($offset, [IO.SeekOrigin]::Begin)
    $bytes = New-Object byte[] $SectorSize
    $read = $source.Read($bytes, 0, $bytes.Length)
    if ($read -ne $bytes.Length) { Fail 'short read from the super backup' }
} finally {
    $source.Close()
}
[IO.File]::WriteAllBytes($restore, $bytes)
$restoreSha = Sha $restore
Write-Host ("backup sector {0}: sha256 {1}" -f $Sector, $restoreSha)

$readArgs = @{
    StartSector = $Sector
    Sectors = 1
    OutFile = $current
    Label = 'super'
    Lun = 0
}
if ($SkipSahara) { $readArgs.SkipSahara = $true }
if ($SkipSahara) { $readArgs.SkipConfigure = $true }
& $ReadScript @readArgs
if (-not $? -or -not (Test-Path -LiteralPath $current)) {
    Fail 'could not read the current super sector'
}

$currentSha = Sha $current
Write-Host ("current sector {0}: sha256 {1}" -f $Sector, $currentSha)
if ($currentSha -eq $restoreSha) {
    Write-Host 'already restored; no write needed' -ForegroundColor Green
    exit 0
}

$currentBytes = [IO.File]::ReadAllBytes($current)
$isProgressRecord = (
    $currentBytes.Length -eq $SectorSize -and
    $currentBytes[0] -eq 0x44 -and
    $currentBytes[1] -eq 0x42 -and
    $currentBytes[2] -eq 0x44 -and
    $currentBytes[3] -eq 0x32 -and
    $currentBytes[4] -eq 0x0b -and
    $currentBytes[5] -eq 0x00 -and
    $currentBytes[6] -eq 0x00 -and
    $currentBytes[7] -eq 0x00
)
if (-not $isProgressRecord) {
    Fail 'current sector is neither the known BP_RECORD nor the backup data'
}

if ($DryRun) {
    Write-Host "dry run: would restore only super LBA $Sector"
    exit 0
}

$writeArgs = @{
    Img = $restore
    StartSector = $Sector
    Label = 'super'
    Lun = 0
    PartitionBytes = $backupLength
}
if ($SkipSahara) { $writeArgs.SkipSahara = $true }
if ($NoReboot) { $writeArgs.NoReboot = $true }
& $WriteScript @writeArgs
if (-not $?) { Fail 'sector restore failed' }
Write-Host ("restored super LBA {0} without rewriting the partition" -f $Sector) -ForegroundColor Green
