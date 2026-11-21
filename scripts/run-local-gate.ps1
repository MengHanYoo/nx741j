param(
    [string]$Distro = "Ubuntu",
    [string]$Tree = "/opt/work/nx741j-build/UniXNU",
    [switch]$RestartWsl
)

$ErrorActionPreference = "Stop"
$repoWin = "E:\default-workspace\z80u-collect\repos\UniXNU-nx741j"
$tmpWin = "E:\default-workspace\z80u-collect\_tmp"
$repoWsl = "/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j"
$log = Join-Path $tmpWin ("local-gate-{0}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss"))

New-Item -ItemType Directory -Path $tmpWin -Force | Out-Null
if ($RestartWsl) {
    & "C:\Program Files\WSL\wsl.exe" --shutdown
}

$commands = @(
    "sudo bash '$repoWsl/scripts/sync-framework-to-wsl.sh'",
    "bash '$repoWsl/scripts/check-wsl-env.sh'",
    "sudo env UNIXNU='$Tree' bash '$repoWsl/scripts/build-and-test.sh' '$Tree'"
) -join " && "

Write-Host "Running local WSL gate..." -ForegroundColor Cyan
Write-Host "log: $log"
& "C:\Program Files\WSL\wsl.exe" -d $Distro -- bash -lc $commands 2>&1 |
    Tee-Object -FilePath $log
$status = $LASTEXITCODE
Write-Host "gate exit: $status"
exit $status
