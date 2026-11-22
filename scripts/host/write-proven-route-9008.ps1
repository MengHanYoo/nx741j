#requires -Version 5.1
<#
  Install the proven NX741J route in one 9008/Firehose session:

    1. verify the live 4K GPT and the current base bytes of every changed root block;
    2. write only the 4K blocks changed by the root bring-up patch;
    3. restore the stock efisp;
    4. write the integrated shim loader boot_a;
    5. read every written region back and compare before reset.

  userdata is not repartitioned and the full 16 GiB root is not rewritten.
#>
param(
    [string]$Boot = 'E:\default-workspace\z80u-collect\repos\nx741j\shim\loader\out-proven-route\loader-boot.img',
    [string]$Efisp = 'E:\default-workspace\z80u-collect\archive\nx741j-stage2-2026-11-08\loader\efisp-stock-rollback.img',
    [string]$DeltaDir = 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route\root-delta',
    [int]$BootStartSector = 119078,
    [int]$EfispStartSector = 748894,
    [int]$TimeoutSec = 300,
    [switch]$NoReboot,
    [switch]$PreflightOnly,
    [switch]$SkipSahara,
    [switch]$SkipConfigure
)

$ErrorActionPreference = 'Stop'
$SectorSize = 4096
$BootPartitionBytes = 100663296
$EfispBytes = 3145728
$ToolDir = 'E:\default-workspace\_tmp\edl-tools'
$Devprg = 'E:\default-workspace\_tmp\edlbf'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Work = Join-Path 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route' ('device-write-' + $Stamp)
$ReportPath = Join-Path $Work 'write-report.json'

function Log([string]$Message) {
    $line = '[' + (Get-Date -Format 'HH:mm:ss') + '] ' + $Message
    Write-Output $line
    Add-Content -Path (Join-Path $Work 'write.log') -Value $line -Encoding UTF8
}

function Fail([string]$Message) {
    Log ('FAILED: ' + $Message)
    throw $Message
}

function Get-EdlPort {
    $device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match '9008' } |
        Select-Object -First 1
    if ($device -and $device.FriendlyName -match 'COM(\d+)') {
        return 'COM' + $matches[1]
    }
    return $null
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-Tool(
    [string]$File,
    [string[]]$Arguments,
    [string]$Tag,
    [int]$LimitSec = 300
) {
    $stdout = Join-Path $Work ($Tag + '.out.txt')
    $stderr = Join-Path $Work ($Tag + '.err.txt')
    Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -WorkingDirectory $Work `
        -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $process.WaitForExit($LimitSec * 1000)) {
        try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch {}
        Fail ($Tag + ' timed out')
    }
    $out = if (Test-Path $stdout) { Get-Content -LiteralPath $stdout -Raw } else { '' }
    $err = if (Test-Path $stderr) { Get-Content -LiteralPath $stderr -Raw } else { '' }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = $out
        Error = $err
    }
}

function Normalize-FhRead(
    [string]$Source,
    [string]$Destination,
    [int]$ExpectedBytes,
    [string]$Tag
) {
    if (-not (Test-Path -LiteralPath $Source)) {
        Fail ($Tag + ': fh_loader produced no read file')
    }
    $bytes = [System.IO.File]::ReadAllBytes($Source)
    $prefix = 0
    if ($bytes.Length -ge 5) {
        $headLen = [Math]::Min(4096, $bytes.Length)
        $head = [Text.Encoding]::ASCII.GetString($bytes, 0, $headLen)
        if ($head.StartsWith('<?xml')) {
            $end = $head.IndexOf('</data>')
            if ($end -lt 0) { Fail ($Tag + ': malformed XML ACK prefix') }
            $prefix = $end + 7
            while ($prefix -lt $bytes.Length -and
                   ($bytes[$prefix] -eq 10 -or $bytes[$prefix] -eq 13)) {
                $prefix++
            }
        }
    }
    $available = $bytes.Length - $prefix
    if ($available -lt $ExpectedBytes) {
        Fail ("{0}: read {1} B, need {2} B" -f $Tag, $available, $ExpectedBytes)
    }
    for ($index = $ExpectedBytes; $index -lt $available; $index++) {
        if ($bytes[$prefix + $index] -ne 0) {
            Fail ($Tag + ': nonzero trailing read padding')
        }
    }
    $result = New-Object byte[] $ExpectedBytes
    [Array]::Copy($bytes, $prefix, $result, 0, $ExpectedBytes)
    [System.IO.File]::WriteAllBytes($Destination, $result)
    if ($prefix -gt 0) {
        Log ("{0}: stripped {1}-byte fh_loader XML ACK" -f $Tag, $prefix)
    }
}

function New-ProgramXml(
    [string]$Name,
    [string]$FileName,
    [int]$Lun,
    [string]$Label,
    [int]$StartSector,
    [int]$Sectors
) {
    $path = Join-Path $Work ($Name + '.xml')
    $xml = '<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="4096" filename="' +
        $FileName + '" physical_partition_number="' + $Lun + '" label="' + $Label +
        '" start_sector="' + $StartSector + '" num_partition_sectors="' + $Sectors +
        '" /></data>'
    Set-Content -LiteralPath $path -Value $xml -Encoding ASCII
    return $path
}

function Invoke-Fh([string]$XmlPath, [string]$Tag, [bool]$ReadBack, [string]$Com) {
    $trace = Join-Path $Work 'port_trace.txt'
    Remove-Item -LiteralPath $trace -Force -ErrorAction SilentlyContinue
    $arguments = @(
        "--port=\\.\$Com",
        '--memoryname=ufs',
        "--search_path=$Work",
        "--sendxml=$XmlPath",
        "--mainoutputdir=$Work",
        '--skip_config',
        '--noprompt',
        '--zlpawarehost=1'
    )
    if ($ReadBack) { $arguments += '--convertprogram2read' }
    $result = Invoke-Tool (Join-Path $ToolDir 'fh_loader.exe') $arguments $Tag $TimeoutSec
    $traceText = if (Test-Path $trace) { Get-Content -LiteralPath $trace -Raw } else { '' }
    $combined = $result.Output + "`n" + $result.Error + "`n" + $traceText
    if ($combined -notmatch 'Finished Successfully|All Finished') {
        $tail = ($combined -split "`n" | Where-Object { $_.Trim() } | Select-Object -Last 10) -join "`n"
        Fail ($Tag + " did not finish successfully`n" + $tail)
    }
    return $result
}

function Read-Region(
    [string]$Com,
    [int]$Lun,
    [string]$Label,
    [int]$StartSector,
    [int]$Sectors,
    [string]$Tag
) {
    $fileName = $Tag + '.bin'
    $xml = New-ProgramXml ($Tag + '-read') $fileName $Lun $Label $StartSector $Sectors
    Remove-Item -LiteralPath (Join-Path $Work $fileName) -Force -ErrorAction SilentlyContinue
    Invoke-Fh $xml ($Tag + '-read') $true $Com | Out-Null
    $normalized = Join-Path $Work ($Tag + '.normalized.bin')
    Normalize-FhRead (Join-Path $Work $fileName) $normalized ($Sectors * $SectorSize) $Tag
    return $normalized
}

function Write-Region(
    [string]$Com,
    [int]$Lun,
    [string]$Label,
    [int]$StartSector,
    [string]$Source,
    [string]$Tag
) {
    $length = (Get-Item -LiteralPath $Source).Length
    if (($length % $SectorSize) -ne 0) { Fail ($Tag + ': source is not sector aligned') }
    $sectors = [int]($length / $SectorSize)
    $fileName = $Tag + '.source.bin'
    Copy-Item -LiteralPath $Source -Destination (Join-Path $Work $fileName) -Force
    $xml = New-ProgramXml ($Tag + '-write') $fileName $Lun $Label $StartSector $sectors
    Invoke-Fh $xml ($Tag + '-write') $false $Com | Out-Null
    $readback = Read-Region $Com $Lun $Label $StartSector $sectors ($Tag + '-verify')
    if (-not (Test-FileEqual $Source $readback)) {
        Fail ($Tag + ': read-back differs from source')
    }
    $sha = Get-Sha256 $Source
    Log ("VERIFY {0}: {1} sectors at LUN{2} {3}, sha256 {4}" -f $Tag, $sectors, $Lun, $StartSector, $sha)
    return [pscustomobject]@{
        tag = $Tag
        lun = $Lun
        label = $Label
        start_sector = $StartSector
        sectors = $sectors
        bytes = $length
        sha256 = $sha
    }
}

function Test-FileEqual([string]$Left, [string]$Right) {
    $leftBytes = [System.IO.File]::ReadAllBytes($Left)
    $rightBytes = [System.IO.File]::ReadAllBytes($Right)
    $leftHash = [Security.Cryptography.SHA256]::Create().ComputeHash($leftBytes)
    $rightHash = [Security.Cryptography.SHA256]::Create().ComputeHash($rightBytes)
    return [Linq.Enumerable]::SequenceEqual([byte[]]$leftHash, [byte[]]$rightHash)
}

function Get-GptUserdata([string]$Com) {
    $path = Read-Region $Com 0 'GPT' 0 6 'gpt-preflight'
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -ne 6 * $SectorSize) { Fail 'GPT primary read length' }
    $header = [Text.Encoding]::ASCII.GetString($bytes, $SectorSize, 8)
    if ($header -ne 'EFI PART') { Fail 'GPT signature' }
    $entryBase = 2 * $SectorSize
    $found = $null
    for ($index = 0; $index -lt 128; $index++) {
        $offset = $entryBase + $index * 128
        $typeNonzero = $false
        for ($byteIndex = 0; $byteIndex -lt 16; $byteIndex++) {
            if ($bytes[$offset + $byteIndex] -ne 0) { $typeNonzero = $true; break }
        }
        if (-not $typeNonzero) { continue }
        $nameBytes = New-Object byte[] 72
        [Array]::Copy($bytes, $offset + 56, $nameBytes, 0, 72)
        $name = [Text.Encoding]::Unicode.GetString($nameBytes).TrimEnd([char]0)
        if ($name -eq 'userdata') {
            $first = [BitConverter]::ToUInt64($bytes, $offset + 32)
            $last = [BitConverter]::ToUInt64($bytes, $offset + 40)
            $found = [pscustomobject]@{ first_lba = $first; last_lba = $last; index = $index }
            break
        }
    }
    if (-not $found) { Fail 'GPT has no userdata partition' }
    return $found
}

function Assert-Input([string]$Path, [string]$ExpectedSha, [long]$ExpectedBytes) {
    if (-not (Test-Path -LiteralPath $Path)) { Fail ('missing input ' + $Path) }
    $item = Get-Item -LiteralPath $Path
    if ($ExpectedBytes -gt 0 -and $item.Length -ne $ExpectedBytes) {
        Fail ("{0}: size {1} != {2}" -f $Path, $item.Length, $ExpectedBytes)
    }
    $sha = Get-Sha256 $Path
    if ($ExpectedSha -and $sha -ne $ExpectedSha) {
        Fail ("{0}: sha256 {1} != {2}" -f $Path, $sha, $ExpectedSha)
    }
    return $sha
}

New-Item -ItemType Directory -Path $Work -Force | Out-Null
Set-Content -LiteralPath (Join-Path $Work 'write.log') -Value '' -Encoding UTF8

foreach ($tool in 'fh_loader.exe', 'QSaharaServer.exe', 'qcedlcmdhelper.exe') {
    if (-not (Test-Path (Join-Path $ToolDir $tool))) { Fail ('missing tool ' + $tool) }
}

$bootSha = Assert-Input $Boot '2523e380b0500bb3e65d012a0626876d4db20b5cc38278ee01c70c05c947cef0' 0
if ((Get-Item $Boot).Length -gt $BootPartitionBytes) { Fail 'boot image exceeds boot_a' }
$efispSha = Assert-Input $Efisp '4df183594a63af28c681e48b1d9fdb0305ee055ea8fde92118a1e0af7a5f6f17' $EfispBytes
$manifestPath = Join-Path $DeltaDir 'manifest.json'
if (-not (Test-Path $manifestPath)) { Fail 'missing root delta manifest' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 1) { Fail 'root delta schema' }
if ($manifest.target_sha256 -ne 'dd7dee484eee2faf03484f5c9726682ed562fe7163dd3a362d167454df280ac4') {
    Fail 'unexpected patched root sha256'
}
if ($manifest.range_count -ne 9 -or @($manifest.ranges).Count -ne 9) {
    Fail 'expected exactly 9 root delta ranges'
}
foreach ($range in $manifest.ranges) {
    $path = Join-Path $DeltaDir $range.file
    $sha = Assert-Input $path $range.sha256 $range.bytes
    if ($range.blocks * $SectorSize -ne $range.bytes) { Fail ($range.file + ': block geometry') }
    if ($range.userdata_start_lba -lt $manifest.userdata_first_lba -or
        ($range.userdata_start_lba + $range.blocks - 1) -gt $manifest.userdata_last_lba) {
        Fail ($range.file + ': outside manifest userdata bounds')
    }
}

$com = Get-EdlPort
if (-not $com) {
    Log ("waiting for 9008 up to {0}s" -f $TimeoutSec)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not $com -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        $com = Get-EdlPort
    }
}
if (-not $com) { Fail 'no 9008 port' }
Log ('9008 on ' + $com)

Get-Process QSaharaServer,fh_loader,qcedlcmdhelper -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

if (-not $SkipSahara) {
    $saharaArgs = @(
        '-p', "\\.\$com",
        '-s', "36:$Devprg\multi_image_qti.mbn",
        '-s', "37:$Devprg\multi_image.mbn",
        '-s', "21:$Devprg\xbl_sc.elf",
        '-s', "60:$Devprg\signed_firmware_soc_view.elf",
        '-s', "59:$Devprg\sequencer_ram.elf",
        '-s', "61:$Devprg\tme_config.elf",
        '-s', "13:$Devprg\prog_firehose_ddr.elf",
        '-s', "38:$Devprg\xbl_config_devprg.elf"
    )
    $sahara = Invoke-Tool (Join-Path $ToolDir 'QSaharaServer.exe') $saharaArgs 'sahara' 180
    $saharaText = $sahara.Output + "`n" + $sahara.Error
    if ($saharaText -notmatch 'Successfully uploaded all images|Sahara protocol completed|FIREHOSE MODE DETECTED') {
        Fail 'Sahara upload failed'
    }
    Log 'Sahara: devprg loaded'
    Start-Sleep -Seconds 2
    $newCom = Get-EdlPort
    if ($newCom) { $com = $newCom }
}

if (-not $SkipConfigure) {
    $configureXml = Join-Path $Work 'configure.xml'
    Set-Content -LiteralPath $configureXml -Encoding ASCII -Value '<?xml version="1.0" encoding="UTF-8" ?><data><configure MemoryName="ufs" Verbose="0" AlwaysValidate="0" MaxDigestTableSizeInBytes="8192" MaxPayloadSizeToTargetInBytes="1048576" ZlpAwareHost="1" SkipStorageInit="0" Oem="ZTE"/></data>'
    $configure = Invoke-Tool (Join-Path $ToolDir 'qcedlcmdhelper.exe') @('-p', $com, '-m', 'xml', '-f', $configureXml) 'configure' 60
    $configureText = $configure.Output + "`n" + $configure.Error
    if ($configureText -match "Tool doesn't come from ZTE") { Fail 'ZTE configure rejected' }
    if ($configureText -notmatch 'ACK') { Fail 'configure produced no ACK' }
    Log 'configure: ZTE/UFS ACK'
}

$gpt = Get-GptUserdata $com
Log ("GPT userdata: first={0} last={1} entry={2}" -f $gpt.first_lba, $gpt.last_lba, $gpt.index)
if ($gpt.first_lba -ne $manifest.userdata_first_lba -or
    $gpt.last_lba -ne $manifest.userdata_last_lba) {
    Fail 'live GPT userdata bounds differ from the pinned extent build'
}

$preflight = @()
foreach ($range in $manifest.ranges) {
    $tag = 'root-preflight-' + [IO.Path]::GetFileNameWithoutExtension($range.file)
    $path = Read-Region $com 0 'userdata' $range.userdata_start_lba $range.blocks $tag
    $sha = Get-Sha256 $path
    if ($sha -ne $range.base_sha256) {
        Fail ("{0}: live base sha256 {1} != {2}" -f $range.file, $sha, $range.base_sha256)
    }
    $preflight += [pscustomobject]@{
        file = $range.file
        start_sector = $range.userdata_start_lba
        blocks = $range.blocks
        base_sha256 = $sha
    }
    Log ("PREFLIGHT {0}: base block matches" -f $range.file)
}

$preflightReport = [ordered]@{
    timestamp = $Stamp
    com = $com
    userdata = $gpt
    boot_sha256 = $bootSha
    efisp_sha256 = $efispSha
    root_delta = $preflight
    preflight_only = [bool]$PreflightOnly
}
$preflightReport | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
if ($PreflightOnly) {
    Log ('PASS preflight only; report ' + $ReportPath)
    exit 0
}

$writes = @()
foreach ($range in $manifest.ranges) {
    $tag = 'root-write-' + [IO.Path]::GetFileNameWithoutExtension($range.file)
    $result = Write-Region $com 0 'userdata' $range.userdata_start_lba (Join-Path $DeltaDir $range.file) $tag
    $writes += $result
}
$writes += Write-Region $com 4 'efisp' $EfispStartSector $Efisp 'efisp-stock'
$writes += Write-Region $com 4 'boot_a' $BootStartSector $Boot 'boot-a-proven'

$gptAfter = Get-GptUserdata $com
if ($gptAfter.first_lba -ne $gpt.first_lba -or $gptAfter.last_lba -ne $gpt.last_lba) {
    Fail 'GPT userdata bounds changed during the write'
}
Log 'GPT unchanged after all writes'

$final = [ordered]@{
    timestamp = $Stamp
    com = $com
    userdata = $gpt
    preflight = $preflight
    writes = $writes
    boot_sha256 = $bootSha
    efisp_sha256 = $efispSha
    root_target_sha256 = $manifest.target_sha256
    reset = -not [bool]$NoReboot
    status = 'verified'
}
$final | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding UTF8

if (-not $NoReboot) {
    $resetXml = Join-Path $Work 'reset.xml'
    Set-Content -LiteralPath $resetXml -Encoding ASCII -Value '<?xml version="1.0" encoding="UTF-8" ?><data><power value="reset"/></data>'
    $reset = Invoke-Tool (Join-Path $ToolDir 'qcedlcmdhelper.exe') @('-p', $com, '-m', 'xml', '-f', $resetXml) 'reset' 30
    $resetText = $reset.Output + "`n" + $reset.Error
    if ($resetText -notmatch 'ACK') { Fail 'reset ACK missing' }
    Log 'reset ACK; device is booting the integrated proven route'
}
Log ('PASS all writes verified; report ' + $ReportPath)
