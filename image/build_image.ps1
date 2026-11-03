# 从原始 BASE 镜像 + image/manifest.json 构建运行时镜像（不在成品镜像上手工改）。
param(
  [string]$Base = "E:\default-workspace\z80u-collect\iosroot-IPSW.img",
  [string]$Out  = "E:\default-workspace\z80u-collect\iosroot-BUILD.img",
  [string]$Repo = "E:\default-workspace\z80u-collect\repos\nx741j"
)
$ErrorActionPreference = "Stop"
$manifest  = Join-Path $Repo "image\manifest.json"
$artifacts = "E:\default-workspace\z80u-collect\image-artifacts"
if (-not (Test-Path $Base)) { throw "BASE missing: $Base" }
if (-not (Test-Path $manifest)) { throw "manifest missing: $manifest" }
if (-not (Test-Path $artifacts)) { throw "artifacts missing: $artifacts" }

Write-Host "=== 1. BASE -> OUT ==="
if (Test-Path $Out) { Remove-Item -LiteralPath $Out -Force }
$t0 = Get-Date
Copy-Item -LiteralPath $Base -Destination $Out
Write-Host ("OUT {0} bytes in {1:N1}s" -f (Get-Item $Out).Length, ((Get-Date) - $t0).TotalSeconds)

Write-Host "=== 2. initramfs ==="
$m  = "/mnt/e/default-workspace/z80u-collect/repos/nx741j/image/manifest.json"
$mk = "/mnt/e/default-workspace/z80u-collect/repos/nx741j/image/make_initramfs.py"
$a  = "/mnt/e/default-workspace/z80u-collect/image-artifacts"
wsl -d Ubuntu -u root -e bash -lc ("python3 " + $mk + " --manifest " + $m + " --artifacts " + $a + " --out /root/imgbuild.gz")
if ($LASTEXITCODE -ne 0) { throw "initramfs build failed" }

Write-Host "=== 3. apply ==="
$img = "/mnt/e/default-workspace/z80u-collect/" + (Split-Path $Out -Leaf)
$apply = "/mnt/e/default-workspace/z80u-collect/repos/nx741j/image/apply_image.sh"
wsl -d Ubuntu -u root -e bash $apply $img
Write-Host "=== 4. verify ==="
$sz = "E:\Nubia_Flash_Brick\中兴家族工具箱1.2.9-beta1\bin\tool\Win\7z.exe"
$l = & $sz l $Out "iOSRoot/System/Library/Extensions/NativeAdrenoMetal.bundle/NativeAdrenoMetal" 2>&1
($l | Select-String "NativeAdrenoMetal\s*$") | Select-Object -First 1
$w = & $sz l $Out "iOSRoot/Library/Wallpaper/Collections/iOS_16~iphone.wallpaperCollection/Wallpapers/7400.WWDC_2022-375w-812h@3x~iphone.wallpaper/7400.WWDC_2022_Background-375w-812h@3x~iphone.ca/assets/*.png" 2>&1
($w | Select-String "\.png\s*$") | ForEach-Object { ($_ -split "\s+")[-1] }
$a2 = & $sz l $Out "iOSRoot/System/Library/LaunchDaemons/com.apple.assetsd.plist" 2>&1
($a2 | Select-String "assetsd.plist") | Select-Object -First 1
Write-Host "=== DONE ==="