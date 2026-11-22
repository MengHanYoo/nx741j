#requires -Version 5.1
<#
  Read file data extents resolved by hfs-inode-ranges.py through an active
  Firehose session. No Sahara/configure and no writes are performed.
#>
param(
    [string]$Ranges = 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route\postboot-log-ranges.json',
    [string]$OutDir = 'E:\default-workspace\z80u-collect\repos\UniXNU-nx741j\out\proven-route\postboot-logs',
    [int]$TimeoutSec = 120
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
        $head = [Text.Encoding]::ASCII.GetString($bytes, 0, [Math]::Min(4096, $bytes.Length))
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
    if (($bytes.Length - $prefix) -lt $Expected) { Fail 'short read' }
    $result = New-Object byte[] $Expected
    [Array]::Copy($bytes, $prefix, $result, 0, $Expected)
    [System.IO.File]::WriteAllBytes($Destination, $result)
}

$com = Get-EdlPort
if (-not $com) { Fail 'no active 9008 port' }
Log ('reading file extents through ' + $com)
$report = Get-Content -LiteralPath $Ranges -Raw | ConvertFrom-Json
$manifest = @()

foreach ($item in $report.items) {
    $index = 0
    foreach ($range in $item.ranges) {
        if ($range.length -le 0) { continue }
        $tag = ('{0}-{1:D3}' -f $item.label, $index)
        $workName = $tag + '.work.bin'
        $fileName = $tag + '.bin'
        $xml = Join-Path $OutDir ($tag + '.xml')
        $xmlText = '<?xml version="1.0" ?><data><program SECTOR_SIZE_IN_BYTES="4096" filename="' +
            $workName + '" physical_partition_number="0" label="userdata" start_sector="' +
            $range.userdata_lba + '" num_partition_sectors="' +
            [int]($range.length / 4096) + '" /></data>'
        Set-Content -LiteralPath $xml -Value $xmlText -Encoding ASCII
        Remove-Item -LiteralPath (Join-Path $OutDir $workName) -Force -ErrorAction SilentlyContinue
        $trace = Join-Path $OutDir ('port_trace-' + $tag + '.txt')
        $stdout = Join-Path $OutDir ($tag + '.out.txt')
        $stderr = Join-Path $OutDir ($tag + '.err.txt')
        $stdoutTmp = $stdout + '.tmp'
        $stderrTmp = $stderr + '.tmp'
        $process = Start-Process -FilePath (Join-Path $ToolDir 'fh_loader.exe') -ArgumentList @(
            "--port=\\.\$com", '--memoryname=ufs', "--search_path=$OutDir",
            "--sendxml=$xml", "--mainoutputdir=$OutDir", '--skip_config',
            '--noprompt', '--zlpawarehost=1', '--convertprogram2read'
        ) -WorkingDirectory $OutDir -NoNewWindow -PassThru `
          -RedirectStandardOutput $stdoutTmp -RedirectStandardError $stderrTmp
        if (-not $process.WaitForExit($TimeoutSec * 1000)) {
            try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch {}
            Fail ($tag + ' timed out')
        }
        Move-Item $stdoutTmp $stdout -Force
        Move-Item $stderrTmp $stderr -Force
        $text = (Get-Content $stdout -Raw) + "`n" + (Get-Content $stderr -Raw)
        if ($text -notmatch 'Finished Successfully|All Finished') {
            Fail ($tag + ' did not finish')
        }
        Normalize-Read (Join-Path $OutDir $workName) (Join-Path $OutDir $fileName) $range.length
        $sha = (Get-FileHash -LiteralPath (Join-Path $OutDir $fileName) -Algorithm SHA256).Hash.ToLowerInvariant()
        $manifest += [pscustomobject]@{
            label = $item.label
            inode = $item.inode
            path = $item.path
            logical = $range.logical
            userdata_lba = $range.userdata_lba
            bytes = $range.length
            file = $fileName
            sha256 = $sha
        }
        Log ("{0}: {1} B LBA {2}" -f $tag, $range.length, $range.userdata_lba)
        $index++
    }
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'manifest.json') -Encoding UTF8
Log 'PASS HFS file extent read'
