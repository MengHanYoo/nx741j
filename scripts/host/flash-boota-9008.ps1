#requires -Version 5.1
<#
  Write boot_a through 9008 (EDL) using the ZTE toolbox sequence.

  Flow (mirrors bin\write.bat of the ZTE family toolbox, the path that worked):
    1. QSaharaServer uploads the eight devprg images
    2. qcedlcmdhelper sends <configure ... Oem="ZTE"/>
    3. fh_loader --sendxml=program.xml --skip_config writes the partition
    4. fh_loader --convertprogram2read reads it back
    5. qcedlcmdhelper sends <power value="reset"/>

  --skip_config is required: without it fh_loader sends its own <configure>
  without Oem="ZTE" and the devprg answers "ERROR: Tool doesn't come from ZTE".

  The toolbox lives under a path with non-ASCII characters, which Windows
  PowerShell 5.1 mangles, so the binaries are located through abl_unlock.elf and
  copied into E:\default-workspace\_tmp\edl-tools. This script stays ASCII-only.
  The work directory is ASCII without 8.3 short names because fh_loader rejects
  the short form with "Invalid input to GetFullDirPath".
#>
param(
    [Parameter(Mandatory = $true)][string]$Img,
    [int]$StartSector = 119078,
    [string]$Label = 'boot_a',
    [int]$Lun = 4,
    [long]$PartitionBytes = 100663296,
    [int]$TimeoutSec = 300,
    [switch]$NoVerify,
    [switch]$NoReboot,
    [switch]$SkipSahara,
    [switch]$SkipConfigure,
    [switch]$Raw,
    [switch]$DryRun,
    [string]$ReadTo,
    [int]$ReadSectors = 32
)

$ErrorActionPreference = 'Stop'
$Devprg = 'E:\default-workspace\_tmp\edlbf'
$ToolDir = 'E:\default-workspace\_tmp\edl-tools'
$WorkRoot = 'E:\default-workspace\z80u-collect\_tmp\flash-work'
$SectorSize = 4096

function Log([string]$m) { Write-Output ('[' + (Get-Date -Format 'HH:mm:ss') + '] ' + $m) }
function Fail([string]$m) { Log ('FAILED: ' + $m); exit 1 }

function Test-PaddedImage([string]$Source, [string]$Readback) {
    # fh_loader sometimes leaves its XML ACK at the start of a converted read.
    $sourceBytes = [System.IO.File]::ReadAllBytes($Source)
    $readBytes = [System.IO.File]::ReadAllBytes($Readback)
    $prefix = 0
    if ($readBytes.Length -ge 8) {
        $headLength = [Math]::Min(4096, $readBytes.Length)
        $head = [Text.Encoding]::ASCII.GetString($readBytes, 0, $headLength)
        if ($head.StartsWith('<?xml')) {
            $end = $head.IndexOf('</data>')
            if ($end -ge 0) {
                $prefix = $end + 7
                while ($prefix -lt $readBytes.Length -and
                       ($readBytes[$prefix] -eq 10 -or $readBytes[$prefix] -eq 13)) {
                    $prefix++
                }
            }
        }
    }
    if ($prefix -ge $readBytes.Length) {
        return [pscustomobject]@{ Match = $false; Prefix = $prefix }
    }

    $available = $readBytes.Length - $prefix
    if ($available -gt $sourceBytes.Length) {
        return [pscustomobject]@{ Match = $false; Prefix = $prefix }
    }
    $sourcePart = New-Object byte[] $available
    $readPart = New-Object byte[] $available
    [Array]::Copy($sourceBytes, 0, $sourcePart, 0, $available)
    [Array]::Copy($readBytes, $prefix, $readPart, 0, $available)
    if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$sourcePart, [byte[]]$readPart)) {
        return [pscustomobject]@{ Match = $false; Prefix = $prefix }
    }

    # A converted read is sector-sized. Padding dropped from the tail must be zero.
    for ($index = $available; $index -lt $sourceBytes.Length; $index++) {
        if ($sourceBytes[$index] -ne 0) {
            return [pscustomobject]@{ Match = $false; Prefix = $prefix }
        }
    }
    return [pscustomobject]@{ Match = $true; Prefix = $prefix }
}

function Get-EdlPort {
    $p = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match '9008' } | Select-Object -First 1
    if ($p -and $p.FriendlyName -match 'COM(\d+)') { return 'COM' + $matches[1] }
    return $null
}

function Get-ZteToolDir {
    # abl_unlock.elf sits in <toolbox>\bin\res\NX741J, the tools in <toolbox>\bin\tool\Win
    $marker = Get-ChildItem 'E:\Nubia_Flash_Brick' -Recurse -File -Filter 'abl_unlock.elf' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $marker) { return $null }
    return (Join-Path $marker.Directory.Parent.Parent.FullName 'tool\Win')
}

function Ensure-Tools {
    if ((Test-Path "$ToolDir\fh_loader.exe") -and (Test-Path "$ToolDir\QSaharaServer.exe") -and
        (Test-Path "$ToolDir\qcedlcmdhelper.exe")) { return }
    $src = Get-ZteToolDir
    if (-not $src -or -not (Test-Path "$src\fh_loader.exe")) { Fail 'ZTE toolbox tool directory not found' }
    New-Item -ItemType Directory -Path $ToolDir -Force | Out-Null
    foreach ($n in 'fh_loader.exe', 'QSaharaServer.exe', 'qcedlcmdhelper.exe') {
        Copy-Item (Join-Path $src $n) (Join-Path $ToolDir $n) -Force
        Log ("tool: {0} ({1} B)" -f $n, (Get-Item (Join-Path $ToolDir $n)).Length)
    }
}

# --- inputs ---------------------------------------------------------------
if (-not (Test-Path $Img)) { Fail "image not found: $Img" }
$item = Get-Item $Img
$bytes = $item.Length
$sha = (Get-FileHash $Img -Algorithm SHA256).Hash.ToLower()
$sectors = [int][math]::Ceiling($bytes / $SectorSize)
$padded = $sectors * $SectorSize
Log ("image {0}  {1} B  {2} sectors  sha256 {3}" -f $item.Name, $bytes, $sectors, $sha.Substring(0, 16))
if ($padded -gt $PartitionBytes) { Fail ("image needs {0} B, partition is {1} B" -f $padded, $PartitionBytes) }

Ensure-Tools

$work = Join-Path $WorkRoot (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Path $work -Force | Out-Null
$target = Join-Path $work 'boot_a.img'
Copy-Item $Img $target -Force
if ($padded -ne $bytes) {
    $fs = [System.IO.File]::Open($target, 'Append')
    $fs.Write((New-Object byte[] ($padded - $bytes)), 0, ($padded - $bytes))
    $fs.Close()
}
Log ("work dir {0}" -f $work)

if ($DryRun) { Log 'dry run: staged only'; exit 0 }

# --- 1. wait for 9008 -----------------------------------------------------
$com = Get-EdlPort
if (-not $com) {
    Log "waiting for 9008 (up to $TimeoutSec s)"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not $com -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $com = Get-EdlPort }
}
if (-not $com) { Fail 'no 9008 port' }
Log "9008 on $com"

# --- 2. Sahara ------------------------------------------------------------
if (-not $SkipSahara) {
    $sLog = Join-Path $work 'sahara.log'
    $saharaArgs = @(
        '-p', "\\.\$com",
        '-s', "36:$Devprg\multi_image_qti.mbn", '-s', "37:$Devprg\multi_image.mbn",
        '-s', "21:$Devprg\xbl_sc.elf", '-s', "60:$Devprg\signed_firmware_soc_view.elf",
        '-s', "59:$Devprg\sequencer_ram.elf", '-s', "61:$Devprg\tme_config.elf",
        '-s', "13:$Devprg\prog_firehose_ddr.elf", '-s', "38:$Devprg\xbl_config_devprg.elf"
    )
    $p = Start-Process -FilePath (Join-Path $ToolDir 'QSaharaServer.exe') -ArgumentList $saharaArgs `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $sLog -RedirectStandardError (Join-Path $work 'sahara.err')
    $t = ''
    if (Test-Path $sLog) { $t = Get-Content $sLog -Raw }
    if ($t -notmatch 'Successfully uploaded all images|Sahara protocol completed') {
        Fail ("sahara did not complete (exit {0}, see {1})" -f $p.ExitCode, $sLog)
    }
    Log 'sahara: devprg loaded'
    Start-Sleep -Seconds 2
    $com2 = Get-EdlPort
    if ($com2) { $com = $com2 }
} else {
    Log 'sahara: skipped'
}

# --- 3. configure ---------------------------------------------------------
if (-not $SkipConfigure) {
    $cfg = Join-Path $work 'configure.xml'
    '<?xml version="1.0" encoding="UTF-8" ?><data><configure MemoryName="ufs" Verbose="0" AlwaysValidate="0" MaxDigestTableSizeInBytes="8192" MaxPayloadSizeToTargetInBytes="1048576" ZlpAwareHost="1" SkipStorageInit="0" Oem="ZTE"/></data>' |
        Set-Content -Path $cfg -Encoding ASCII
    Log 'configure: Oem="ZTE"'
    $cfgOut = Join-Path $work 'configure.out.txt'
    $p = Start-Process -FilePath (Join-Path $ToolDir 'qcedlcmdhelper.exe') -ArgumentList @('-p', $com, '-m', 'xml', '-f', $cfg) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $cfgOut -RedirectStandardError (Join-Path $work 'configure.err.txt')
    $cfgText = ''
    if (Test-Path $cfgOut) { $cfgText = Get-Content $cfgOut -Raw }
    if ($cfgText -match 'Tool doesn') { Fail 'configure rejected by the programmer' }
    $storage = 'no storage line'
    if ($cfgText -match 'Storage type set to value UFS') { $storage = 'storage=UFS' }
    Log ("configure: helper exit {0}, {1}" -f $p.ExitCode, $storage)
} else {
    Log 'configure: skipped'
}

# --- 3b. read-only dump ----------------------------------------------------
if ($ReadTo) {
    $dumpName = Split-Path $ReadTo -Leaf
    $readXml = Join-Path $work 'dump.xml'
    ('<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="{0}" filename="{5}" physical_partition_number="{4}" label="{3}" start_sector="{1}" num_partition_sectors="{2}" /></data>' -f $SectorSize, $StartSector, $ReadSectors, $Label, $Lun, $dumpName) |
        Set-Content -Path $readXml -Encoding ASCII
    Log ('dump: {0} sectors at {1}, LUN{2} {3}' -f $ReadSectors, $StartSector, $Lun, $Label)
    Start-Process -FilePath (Join-Path $ToolDir 'fh_loader.exe') -ArgumentList @(
        "--port=\\.\$com", '--memoryname=ufs', "--search_path=$work", "--sendxml=$readXml",
        "--mainoutputdir=$work", '--skip_config', '--noprompt', '--convertprogram2read'
    ) -NoNewWindow -Wait | Out-Null
    $dump = Join-Path $work $dumpName
    if (-not (Test-Path $dump)) { Fail 'dump: no readback file' }
    Copy-Item $dump $ReadTo -Force
    Log ('dump: wrote {0} ({1} bytes)' -f $ReadTo, (Get-Item $ReadTo).Length)
    if (-not $NoReboot) {
        $rst = Join-Path $work 'reset.xml'
        '<?xml version="1.0" ?><data><power value="reset"/></data>' | Set-Content -Path $rst -Encoding ASCII
        Start-Process -FilePath (Join-Path $ToolDir 'qcedlcmdhelper.exe') -ArgumentList @('-p', $com, '-m', 'xml', '-f', $rst) -NoNewWindow -Wait | Out-Null
    }
    exit 0
}

# --- 4. program -----------------------------------------------------------
$progXml = Join-Path $work 'program.xml'
('<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="{0}" filename="boot_a.img" physical_partition_number="{4}" label="{3}" start_sector="{1}" num_partition_sectors="{2}" /></data>' -f $SectorSize, $StartSector, $sectors, $Label, $Lun) |
    Set-Content -Path $progXml -Encoding ASCII
Log ("program: {0} LUN{4} sector {1}, {2} sectors, {3} B" -f $Label, $StartSector, $sectors, $padded, $Lun)
$fhOut = Join-Path $work 'fh.out.txt'
$p = Start-Process -FilePath (Join-Path $ToolDir 'fh_loader.exe') -ArgumentList @(
    "--port=\\.\$com", '--memoryname=ufs', "--search_path=$work", "--sendxml=$progXml",
    "--mainoutputdir=$work", '--skip_config', '--showpercentagecomplete', '--noprompt'
) -NoNewWindow -Wait -PassThru -RedirectStandardOutput $fhOut -RedirectStandardError (Join-Path $work 'fh.err.txt')
$fhText = ''
if (Test-Path $fhOut) { $fhText = Get-Content $fhOut -Raw }
$trace = ''
if (Test-Path (Join-Path $work 'port_trace.txt')) { $trace = Get-Content (Join-Path $work 'port_trace.txt') -Raw }
if (($fhText -notmatch 'Finished Successfully|All Finished') -and ($trace -notmatch 'Finished Successfully')) {
    if ($fhText) { ($fhText -split "`n" | Where-Object { $_.Trim() } | Select-Object -Last 8) | ForEach-Object { Log ('    ' + $_) } }
    Fail ("program did not report success (exit {0}, see {1})" -f $p.ExitCode, $work)
}
Log ("program: {0} written" -f $Label)

# --- 5. read back ---------------------------------------------------------
if (-not $NoVerify) {
    $readXml = Join-Path $work 'read.xml'
    ('<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="{0}" filename="readback_boot_a.img" physical_partition_number="{4}" label="{3}" start_sector="{1}" num_partition_sectors="{2}" /></data>' -f $SectorSize, $StartSector, $sectors, $Label, $Lun) |
        Set-Content -Path $readXml -Encoding ASCII
    Log 'verify: reading back'
    Start-Process -FilePath (Join-Path $ToolDir 'fh_loader.exe') -ArgumentList @(
        "--port=\\.\$com", '--memoryname=ufs', "--search_path=$work", "--sendxml=$readXml",
        "--mainoutputdir=$work", '--skip_config', '--noprompt', '--convertprogram2read'
    ) -NoNewWindow -Wait | Out-Null
    $rb = Join-Path $work 'readback_boot_a.img'
    if (Test-Path $rb) {
        $rbSha = (Get-FileHash $rb -Algorithm SHA256).Hash.ToLower()
        Log ("verify: readback sha256 " + $rbSha.Substring(0, 16))
        $check = Test-PaddedImage $target $rb
        if (-not $check.Match) {
            Fail 'read-back does not match the image'
        }
        if ($check.Prefix -gt 0) {
            Log ("verify: ignored {0}-byte fh_loader XML ACK prefix" -f $check.Prefix)
        }
        Log 'verify: match'
    } else {
        Log 'verify: read-back file missing, skipped'
    }
}

# --- 6. reset -------------------------------------------------------------
if (-not $NoReboot) {
    $rst = Join-Path $work 'reset.xml'
    '<?xml version="1.0" ?><data><power value="reset"/></data>' | Set-Content -Path $rst -Encoding ASCII
    Log 'reset'
    Start-Process -FilePath (Join-Path $ToolDir 'qcedlcmdhelper.exe') -ArgumentList @('-p', $com, '-m', 'xml', '-f', $rst) `
        -NoNewWindow -Wait | Out-Null
}
Log ('done: boot_a = ' + $item.Name + ' sha256 ' + $sha.Substring(0, 16))


