#requires -Version 5.1
<#
  Read the live HFS+ metadata forks through an already configured Firehose
  session. This does not upload Sahara or send configure and never writes.
#>
param(
    [string]$Ranges = 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route\postboot-hfs-ranges.json',
    [string]$OutDir = 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route\postboot-hfs',
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
$ToolDir = 'E:\default-workspace\_tmp\edl-tools'
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

function Log([string]$Message) {
    $line = '[' + (Get-Date -Format 'HH:mm:ss') + '] ' + $Message
    Write-Output $line
    Add-Content -LiteralPath (Join-Path $OutDir 'read.log') -Value $line -Encoding UTF8
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

function Normalize-Read([string]$Source, [string]$Destination, [long]$Expected) {
    if (-not (Test-Path $Source)) { Fail ('missing read ' + $Source) }
    $bytes = [System.IO.File]::ReadAllBytes($Source)
    $prefix = 0
    if ($bytes.Length -ge 5) {
        $headLen = [Math]::Min(4096, $bytes.Length)
        $head = [Text.Encoding]::ASCII.GetString($bytes, 0, $headLen)
        if ($head.StartsWith('<?xml')) {
            $end = $head.IndexOf('</data>')
            if ($end -lt 0) { Fail 'malformed XML ACK prefix' }
            $prefix = $end + 7
            while ($prefix -lt $bytes.Length -and
                   ($bytes[$prefix] -eq 10 -or $bytes[$prefix] -eq 13)) {
                $prefix++
            }
        }
    }
    $available = $bytes.Length - $prefix
    if ($available -lt $Expected) {
        Fail ("read {0} B, need {1} B" -f $available, $Expected)
    }
    for ($i = $Expected; $i -lt $available; $i++) {
        if ($bytes[$prefix + $i] -ne 0) { Fail 'nonzero read padding' }
    }
    $result = New-Object byte[] $Expected
    [Array]::Copy($bytes, $prefix, $result, 0, $Expected)
    [System.IO.File]::WriteAllBytes($Destination, $result)
}

$com = Get-EdlPort
if (-not $com) { Fail 'no active 9008 port; read one sector first to load Firehose' }
Log ('reading through active Firehose on ' + $com)
$report = Get-Content -LiteralPath $Ranges -Raw | ConvertFrom-Json
$manifest = @()

foreach ($itemName in @('volume_header', 'allocation', 'extents', 'catalog', 'attributes')) {
    $item = $report.items.$itemName
    if (-not $item) { Fail ('missing item ' + $itemName) }
    $index = 0
    foreach ($range in $item.ranges) {
        $tag = ('{0}-{1:D3}' -f $itemName, $index)
        $fileName = $tag + '.bin'
        $workName = $tag + '.work.bin'
        $xml = Join-Path $OutDir ($tag + '.xml')
        $xmlText = '<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="4096" filename="' +
            $workName + '" physical_partition_number="0" label="userdata" start_sector="' +
            $range.userdata_lba + '" num_partition_sectors="' +
            [int]($range.length / 4096) + '" /></data>'
        Set-Content -LiteralPath $xml -Value $xmlText -Encoding ASCII
        Remove-Item -LiteralPath (Join-Path $OutDir $workName) -Force -ErrorAction SilentlyContinue
        $trace = Join-Path $OutDir 'port_trace.txt'
        Remove-Item -LiteralPath $trace -Force -ErrorAction SilentlyContinue
        $stdout = Join-Path $OutDir ($tag + '.out.txt')
        $stderr = Join-Path $OutDir ($tag + '.err.txt')
        $process = Start-Process -FilePath (Join-Path $ToolDir 'fh_loader.exe') -ArgumentList @(
            "--port=\\.\$com", '--memoryname=ufs', "--search_path=$OutDir",
            "--sendxml=$xml", "--mainoutputdir=$OutDir", '--skip_config',
            '--noprompt', '--zlpawarehost=1', '--convertprogram2read'
        ) -WorkingDirectory $OutDir -NoNewWindow -PassThru `
          -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (-not $process.WaitForExit($TimeoutSec * 1000)) {
            try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch {}
            Fail ($tag + ' timed out')
        }
        $text = ''
        if (Test-Path $stdout) { $text += Get-Content $stdout -Raw }
        if (Test-Path $stderr) { $text += Get-Content $stderr -Raw }
        if (Test-Path $trace) { $text += Get-Content $trace -Raw }
        if ($text -notmatch 'Finished Successfully|All Finished') {
            Fail ($tag + ' did not finish')
        }
        Normalize-Read (Join-Path $OutDir $workName) (Join-Path $OutDir $fileName) $range.length
        $sha = (Get-FileHash -LiteralPath (Join-Path $OutDir $fileName) -Algorithm SHA256).Hash.ToLowerInvariant()
        $manifest += [pscustomobject]@{
            item = $itemName
            logical = $range.logical
            userdata_lba = $range.userdata_lba
            bytes = $range.length
            file = $fileName
            sha256 = $sha
        }
        Log ("{0}: {1} B logical 0x{2:x} LBA {3} sha256 {4}" -f
            $tag, $range.length, $range.logical, $range.userdata_lba, $sha)
        $index++
    }
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'manifest.json') -Encoding UTF8
Log 'PASS HFS metadata read'
