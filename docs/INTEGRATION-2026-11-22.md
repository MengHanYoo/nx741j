# NX741J 整合记录（2026-11-22）

本文件记录当前可复现源码、框架补丁、产物和验证边界。设备仓库可以单独
克隆，然后将固定 pin 的框架补丁应用到 UniXNU。

## 结果摘要

| 项 | 状态 | 证据 |
|---|---|---|
| 工作区、输入和 WSL 磁盘在 E 盘 | 完成 | 旧 C 盘树归档；WSL VHDX 为 `E:\wsl\Ubuntu\ext4.vhdx` |
| WSL 冷启动配置 | 通过 | `.wslconfig` 134 B、UTF-8/CRLF；重启后不再出现旧警告 |
| 构建资源 | 完成 | kernel 16 jobs、smoke 8 jobs、QEMU 16 GiB / 8C16T（1 socket × 8 cores × 2 threads），均可覆盖 |
| 4K XNU 内核 | 通过 | 归档、native link 和 prelink 成功 |
| PMGR | 主机 ABI 完成 | `ShengPlatformPower` 340 项审计通过，`_sheng_pmgr_run` 已链接 |
| 正常 UFS 根 payload | 通过 | `native-ufs-root=1` + `sheng-pmgr=2` 通过 payload 检查 |
| QEMU 引导门禁 | 通过 | `BootEntry`、`MARK #29`、`EBS ok`；panic=0 |
| 9008 镜像安装 | 已完成 | 根镜像、锚点、payload、FD、efisp-p7 全部写入并回读 |
| 真机启动 | 未通过 | 捕获日志已过 VM/kext collection 并进入 NativeUfs；复位后为 `QUSB_BULK` |
| loader 进度记录 | 已修源码 | 旧 loader 的 `gRawBase=0` 误写 `super` LBA `20224`；新 loader 为 `73b56bf2…`，待写入 |
| 最终记录 | 完成 | 本文件、`STATUS-2026-11-22.md`、`OPEN-ITEMS.md` 已更新 |

## 关键修复

### IOStorageFamily

保留 Apple 原 bundle，只对
`IOBlockStorageDriver::instantiateMediaObject()` 的 ADRP/ADD 空字符串
加载点注入 `Apple_APFS`。补丁前必须匹配 8 字节原始指令
`a7bfff90e7ec1691`，并验证除该点外的可执行字节不变。

### 动态根锚点

根镜像不再依赖固定 `nxroot-lba`。尾部 4 KiB 的
`NXROOTP1` 锚点绑定：

- userdata 分区 first/last LBA；
- manifest LBA；
- 镜像字节数和扇区数；
- manifest SHA-256；
- 锚点前 96 字节自己的 SHA-256。

### VerifiedRoot

两个原始窗口的 544 字节签名头编译进 `Policy.o`；每个窗口的块哈希表作为
version-2 `<window>.vroot` 对象放进 `userdata-root.img`。宿主 C/RSA 验证器
逐块通过了 OS 和 SystemOS。

### PMGR

设备 package 提供
`package/sheng/build-sheng-platform-power.py`，框架 wrapper
`scripts/build-sheng-platform-power.py` 在组装树中执行它。设备补丁
`patches/xnu/nx741j-pmgr-hook.patch` 在 `bsd_init` 中调用
`sheng_pmgr_run()`。

当前 ABI 报告：

```text
virtual_entries: 340
runtime_qualified: false
original_executable_modified: false
not a functional power backend
archive sha256: bb7e253cd9a945e34985db2078e0f7d4ff5978280883915bdb9a35a697e11032
```

因此 PMGR 可以构建、链接并接受正常根 payload 的启动参数，但不能替代真机
电源验证。

## 最终产物

主机产物放在 `out/final-pmgr/`：

| 文件 | SHA-256 |
|---|---|
| `kernel.macho` | `255cdf323d1d00bc5e077a628d9f6687c6f354de7dc2d9739f2923a70dbd3c9b` |
| `payload.img` | `8801fbd728c35227c193d2aac8dfcf1302ea02a5b2f47c55b07c19f9ab0de95c` |
| `payload-probe12.img` | `fa777aeb86eab4b60c573b3086d2b309ffeed1972f00b4a91e18c34337cf0036` |
| `SILICIUM_UEFI.fd` | `3ce5c6d2c61891975c52846358bff3f3d9744413d61fd732ea48f7507a49a009` |
| `boot.img` | `dbb6c35c9e133c35fe3b7c2d52a4b129239db68ca3d3018309acce7aa10e004d` |
| `pmgr-report.json` | `6ce58fe5e41e6eb018010824199985ca125b98da527c688f345d93945fab5ee6` |
| `efisp-p7.img`（修复版，待写入） | `73b56bf2b2192706ec724974f832457d5b95ab1382038e3442bc392127b4b246` |

`kernel.json`、`kernel.log`、`pmgr-build.log`、`qemu-serial.log` 和
`qemu-int.log` 也保留在同一目录。`out/` 是主机产物目录，不表示设备已刷入。

## 真机写入与观测

9008 实测通过：

- `userdata-root.img` 写入 `246662139..248724071`，全量回读 SHA-256
  `f45c3288b555a43f945c78a8c75ad56343039f3f5f5350fe57316c64539d2b70`；
- 4 KiB `NXROOTP1` 锚点写入 `248838138`，回读一致；
- 旧 normal-root payload、FD 和旧 `efisp-p7.img` 分别写入 rawdump/efisp
  并回读一致；
- probe=12 payload `8221c727…` 于 13:13 写入 rawdump，64 MiB 完整回读一致。

重新进入 9008 后，旧 loader 捕获的日志显示 XNU 已完成 early VM、OSLog 和
kext collection，并到达：

```text
NATIVE_UFS start provider=nubia,nx741j
NATIVE_UFS bringup: enter
NATIVE_UFS bringup: hc=fffffff6bc035000 cap=d5c7031f
```

复位后设备没有出现在 ADB，而是以
`QUSB_BULK_CID:046A_SN:098F0FC3`（`VID_19D2&PID_0112`）出现在 Windows，
没有 9008 COM 端口。真实启动未通过，详细信息见
[DEVICE-TEST-2026-11-22.md](DEVICE-TEST-2026-11-22.md)。

旧 loader 的 `write_progress()` 从未设置 `gRawBase`，所以约定位置
`4764200` 是全零，记录被写到绝对 LBA `20224`。该 LBA 位于 `super`。
修复后的 loader 显式设置 rawdump 基址并启用 UFS snapshot；
`scripts/host/repair-super-progress-sector.ps1` 可从 2026-11-09 的
`super.img` 备份只恢复受影响 4 KiB，且在写入前校验当前 BP_RECORD。

## 复现命令

```sh
git clone git@github.com:MengHanYoo/nx741j.git UniXNU-nx741j
git clone https://github.com/UniXNU/UniXNU.git
git -C UniXNU checkout ba9e14fb46cff0f2d3e8b1a006bd9f849cbd5ce6
./UniXNU-nx741j/scripts/apply-framework-patch.sh UniXNU

cd UniXNU
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j
./unixnu tree -D ../UniXNU-nx741j
./unixnu kernel -D ../UniXNU-nx741j -j 16
./scripts/build-payload-modes.sh /path/to/UniXNU
./unixnu firmware -D ../UniXNU-nx741j
./unixnu bootimg -D ../UniXNU-nx741j
```

框架补丁为
`patches/framework/UniXNU-ba9e14fb-nx741j.patch`，SHA-256
`e58a1f1c72e9269423edba2728444a509efee44a833d4d090ca421ac85f455d3`。
本轮在一个新建的 no-hardlinks clone 上完成了 `apply --check`、应用和反向校验。

## 边界

- 根镜像、尾锚点、payload、FD 和旧 loader 已写并回读；捕获日志到
  NativeUfs，但尚未观察 APFS 选卷和 `launchd`。
- 下次进入 9008 必须先修复 `super` 的已知单扇区误写，再写入新 loader
  并读取新日志。
- PMGR 只完成 ABI/链接资格，原始 nub 运行仍未硬件合格。
- QEMU 只验证引导链交接，不模拟面板、UFS、TouchCom 或 PMGR。
- smoke 六场景仍是独立套件；旧 AAVMF/TCG 偶发卡死没有伪装成通过。
- 第 7 步和持久 Data/性能按用户要求冻结到能启动 iOS 后。

## 安全与边界

- `native-ufs-noverify` 和 `nxroot-lba` 不在最终源码或内核中。
- owner 私钥位于
  `E:\default-workspace\z80u-collect\secrets\nx741j-owner.pem`，
  不提交到 Git。
- 未安装锚点前不能只写 `NXROOTP1`；新镜像必须先完整写入并回读，避免
  manifest SHA、长度或分区边界不一致。
