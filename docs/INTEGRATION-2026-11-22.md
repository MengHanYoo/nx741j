# NX741J 整合记录（2026-11-22）

本文件记录当前可复现源码、框架补丁、产物和验证边界。设备仓库可以单独
克隆，然后将固定 pin 的框架补丁应用到 UniXNU。

## 结果摘要

| 项 | 状态 | 证据 |
|---|---|---|
| 工作区、输入和 WSL 磁盘在 E 盘 | 完成 | 旧 C 盘树归档；WSL VHDX 为 `E:\wsl\Ubuntu\ext4.vhdx` |
| WSL 冷启动配置 | 通过 | `.wslconfig` 134 B、UTF-8/CRLF；重启后不再出现旧警告 |
| 构建资源 | 完成 | kernel 16 jobs、smoke 8 jobs、QEMU 16 GiB/16 vCPU，均可独立覆盖 |
| 4K XNU 内核 | 通过 | 归档、native link 和 prelink 成功 |
| PMGR | 主机 ABI 完成 | `ShengPlatformPower` 340 项审计通过，`_sheng_pmgr_run` 已链接 |
| 正常 UFS 根 payload | 通过 | `native-ufs-root=1` + `sheng-pmgr=2` 通过 payload 检查 |
| QEMU 引导门禁 | 通过 | `BootEntry`、`MARK #29`、`EBS ok`；panic=0 |
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
```

因此 PMGR 可以构建、链接并接受正常根 payload 的启动参数，但不能替代真机
电源验证。

## 最终产物

主机产物放在 `out/final-pmgr/`：

| 文件 | SHA-256 |
|---|---|
| `kernel.macho` | `14e63ca1526f9b956fb9a41b33269b7a13390937a7a743bbba17009e9476207a` |
| `payload.img` | `219817f9c2428b0c3cadf758228462569f257ce63b5c47b8a44ddd3abcd114b0` |
| `SILICIUM_UEFI.fd` | `3ce5c6d2c61891975c52846358bff3f3d9744413d61fd732ea48f7507a49a009` |
| `boot.img` | `8074e86236ae3e158fa30b76d0a1085206aecb80d27e6396f55fe30d4fdb5a9b` |
| `pmgr-report.json` | `aed984979c4b864b581d1877d922f960ce48faacedc827a3e0278443202bf0a9` |

`kernel.json`、`kernel.log`、`pmgr-build.log`、`qemu-serial.log` 和
`qemu-int.log` 也保留在同一目录。`out/` 是主机产物目录，不表示设备已刷入。

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
./unixnu payload -D ../UniXNU-nx741j
./unixnu firmware -D ../UniXNU-nx741j
```

框架补丁为
`patches/framework/UniXNU-ba9e14fb-nx741j.patch`，SHA-256
`e58a1f1c72e9269423edba2728444a509efee44a833d4d090ca421ac85f455d3`。
本轮在一个新建的 no-hardlinks clone 上完成了 `apply --check`、应用和反向校验。

## 边界

- 没有 ADB/9008 设备，未写根镜像或尾锚点，未观察 APFS 选卷和 `launchd`。
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
