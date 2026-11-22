#requires -Version 5.1
<#
  Read a partition slice over 9008 (Sahara + ZTE configure + fh_loader read).
  ASCII-only; tools staged in E:\default-workspace\_tmp\edl-tools.
#>
param(
    [Parameter(Mandatory = $true)][int]$StartSector,
    [Parameter(Mandatory = $true)][int]$Sectors,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$Label = 'partition',
    [int]$Lun = 4,
    [int]$TimeoutSec = 300,
    [switch]$SkipSahara,
    [switch]$SkipConfigure
)

$ErrorActionPreference = 'Stop'
$Devprg = 'E:\default-workspace\_tmp\edlbf'
$ToolDir = 'E:\default-workspace\_tmp\edl-tools'
$WorkRoot = 'E:\default-workspace\z80u-collect\_tmp\flash-work'
$SectorSize = 4096

function Log([string]$m) { Write-Output ('[' + (Get-Date -Format 'HH:mm:ss') + '] ' + $m) }
function Fail([string]$m) { Log ('FAILED: ' + $m); exit 1 }
function Get-EdlPort {
    $p = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match '9008' } | Select-Object -First 1
    if ($p -and $p.FriendlyName -match 'COM(\d+)') { return 'COM' + $matches[1] }
    return $null
}

if (-not (Test-Path "$ToolDir\fh_loader.exe")) { Fail 'tools missing; run flash-boota-9008.ps1 once first' }

$work = Join-Path $WorkRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Path $work -Force | Out-Null

$com = Get-EdlPort
if (-not $com) {
    Log "waiting for 9008 (up to $TimeoutSec s)"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not $com -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $com = Get-EdlPort }
}
if (-not $com) { Fail 'no 9008 port' }
Log "9008 on $com"

if (-not $SkipSahara) {
    $sLog = Join-Path $work 'sahara.log'
    $args1 = @('-p', "\\.\$com",
        '-s', "36:$Devprg\multi_image_qti.mbn", '-s', "37:$Devprg\multi_image.mbn",
        '-s', "21:$Devprg\xbl_sc.elf", '-s', "60:$Devprg\signed_firmware_soc_view.elf",
        '-s', "59:$Devprg\sequencer_ram.elf", '-s', "61:$Devprg\tme_config.elf",
        '-s', "13:$Devprg\prog_firehose_ddr.elf", '-s', "38:$Devprg\xbl_config_devprg.elf")
    $p = Start-Process -FilePath (Join-Path $ToolDir 'QSaharaServer.exe') -ArgumentList $args1 `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $sLog -RedirectStandardError (Join-Path $work 'sahara.err')
    $t = ''
    if (Test-Path $sLog) { $t = Get-Content $sLog -Raw }
    if ($t -notmatch 'Successfully uploaded all images|Sahara protocol completed') { Fail "sahara failed (see $sLog)" }
    Log 'sahara: devprg loaded'
    Start-Sleep -Seconds 2
    $com2 = Get-EdlPort
    if ($com2) { $com = $com2 }
}

if (-not $SkipConfigure) {
    $cfg = Join-Path $work 'configure.xml'
    '<?xml version="1.0" encoding="UTF-8" ?><data><configure MemoryName="ufs" Verbose="0" AlwaysValidate="0" MaxDigestTableSizeInBytes="8192" MaxPayloadSizeToTargetInBytes="1048576" ZlpAwareHost="1" SkipStorageInit="0" Oem="ZTE"/></data>' |
        Set-Content -Path $cfg -Encoding ASCII
    $p = Start-Process -FilePath (Join-Path $ToolDir 'qcedlcmdhelper.exe') -ArgumentList @('-p', $com, '-m', 'xml', '-f', $cfg) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput (Join-Path $work 'cfg.out') -RedirectStandardError (Join-Path $work 'cfg.err')
    Log ("configure: exit {0}" -f $p.ExitCode)
}

$readXml = Join-Path $work 'read.xml'
('<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="4096" filename="read.bin" physical_partition_number="{4}" label="{3}" start_sector="{1}" num_partition_sectors="{2}" /></data>' -f $SectorSize, $StartSector, $Sectors, $Label, $Lun) |
    Set-Content -Path $readXml -Encoding ASCII

Log ("read: {0} sectors at {1} -> {2}" -f $Sectors, $StartSector, $OutFile)
$fhOut = Join-Path $work 'fh.out.txt'
Start-Process -FilePath (Join-Path $ToolDir 'fh_loader.exe') -ArgumentList @(
    "--port=\\.\$com", '--memoryname=ufs', "--search_path=$work", "--sendxml=$readXml",
    "--mainoutputdir=$work", '--skip_config', '--noprompt', '--convertprogram2read'
) -NoNewWindow -Wait | Out-Null

$src = Join-Path $work 'read.bin'
if (-not (Test-Path $src)) { Fail "read failed; see $work" }
$outParent = Split-Path -Parent $OutFile
if ($outParent -and -not (Test-Path -LiteralPath $outParent)) {
    New-Item -ItemType Directory -Path $outParent -Force | Out-Null
}
Copy-Item $src $OutFile -Force
$sha = (Get-FileHash $OutFile -Algorithm SHA256).Hash.ToLower()
$fs = [System.IO.File]::OpenRead($OutFile)
$take = [int][math]::Min(1048576, $fs.Length)
$bytes = New-Object byte[] $take
[void]$fs.Read($bytes, 0, $take)
$fs.Close()
$nonzero = 0
foreach ($b in $bytes) { if ($b -ne 0) { $nonzero++ } }
Log ("read: {0} B sha256 {1} first1MiB_nonzero={2}" -f (Get-Item $OutFile).Length, $sha.Substring(0, 16), $nonzero)
