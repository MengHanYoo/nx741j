# Data A Live 固定 extent 根卷整合（2026-11-22）

本文记录当前主线。旧的 ShengUfs + VerifiedRoot/APFS 选卷记录只作为调查历史保留；
用户已明确纠正：根卷必须从原始 iPhone X IPSW 重新生成，不能使用
`iosroot-GOLDEN.img`。

## 固定输入

| 输入 | 实值 |
|---|---|
| 原始 IPSW | `iPhone10,3,iPhone10,6_16.7.16_20H392_Restore (1).ipsw`，6,171,035,648 B，SHA-256 `ece1194f7e233e9109c5790a8d23a4564a10a254e15ec5b09978a343265568fb` |
| 实机 FDT | `stock/dt/live_fdt.dtb`，911,287 B，SHA-256 `a5c109c0ca6fb30ebe34b915ea090ea4494fe000aadda92042c8575d5b2a8292` |
| OS | `087-86698-044.dmg`，4,730,814,044 B，SHA-256 `80f426d5b8fca8552f95be427434f1911dbb4c2c6d28aad94f2426ddd0c92483` |
| SystemOS | `087-86593-047.dmg`，2,743,074,816 B，SHA-256 `9cd738f3b0169055549df84ad6d8d13e50a2da39af618ce0522ca4e06df86849` |
| AppOS | `087-86715-047.dmg`，10,485,760 B，SHA-256 `391f2df322c3172b021b2874f6da5c3e78b70b3df8f502e1a4af2b1d715c4608` |
| D22AP identity | `repos/nx741j/tools/d22ap_raw.adt`，186,084 B，SHA-256 `9e670987c80493bd69aba7be62e3179c04663324d9249b0973675baac175dbf1` |
| 固定 extent 表 | `_tmp/nx741j-iosroot-extents-20261122.json`，39 段 |

三个 DMG 已直接从原始 IPSW 条目流式重算 SHA-256，结果与上表及构建 manifest
完全一致。因此根卷输入不是旧镜像复用。

根卷流程固定为：

```text
原始 IPSW APFS images
  -> ios-fuxi/tools/mkiosroot.sh
  -> fuxi-stage-root.py
  -> sweet-root-tweaks.py
  -> case-sensitive journaled HFSX
```

`iosroot-GOLDEN.img` 没有参与生成、ADT、QEMU 或真机写入。

## 已生成根卷

```text
E:\default-workspace\z80u-collect\iosroot-artifacts\
  iosroot-NX741J-20H392-20261122.img
  iosroot-NX741J-20H392-20261122-174607.json
```

| 项 | 结果 |
|---|---|
| 大小 | 17,179,869,184 B（16 GiB） |
| SHA-256 | `7fd949bb026ef84d22bf316f28c41234358e41c65e4f393e1a3d1ffe1731b6ea` |
| 文件系统 | 区分大小写、带日志的 HFSX |
| 卷名 | `iOSRoot` |
| `/sbin/launchd` | UID/GID 0:0，`0755`，541,600 B，SHA-256 `12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c` |
| SystemVersion | `16.7.16 / 20H392` |
| launchd jobs | 530 |
| 软件 keybag | `/private/var/keybags/systembag.kb`，`0600` |

该镜像已以 `loop,ro` 只读挂载并逐项检查上述文件，然后卸载。

### HFS 检查说明

`fsck.hfsplus -f -y` 报告 `The volume iOSRoot appears to be OK`。随后 Linux
`hfsprogs 540.1` 的只读复查仍会报：

```text
Volume header needs minor repair
(1, 0)
```

同一现象可在刚 `mkfs.hfsplus`、干净挂载/卸载的测试卷上复现，而只读挂载、目录、
可执行 `launchd`、SystemVersion 和 plist 均正常。因此构建脚本只接受这个精确的
Linux 主/备卷头复查偏差，其他错误仍 fail-closed。

## Extent 表

`pinfile` 生成的 39 段 extent：

| 项 | 结果 |
|---|---|
| 文件大小 | 16 GiB |
| 逻辑覆盖 | 17,179,869,184 B，连续 |
| ADT 编码 | 117 个 little-endian u64，936 B |
| 物理范围（userdata 相对） | `0xd4000000..0x68d200000` |
| 最高 DMA 地址 | 小于 apps-SMMU 的 36 位窗口 |
| flags | `0x1088` / `0x1089`，即 F2FS `ENCODED | DATA_ENCRYPTED | MERGED [| LAST]` |

该 flags 只表示 Android 文件系统读取时加密/编码；raw 块就是 iOS 明文 HFSX。

## 内核代码路径

| 文件 | 作用 |
|---|---|
| `package/sweet-ufs/sweet_ufs.c` | data-a-live UFSHCI 轮询驱动、`ufsroot0`、固定 extent 映射、extent 内写保护 |
| `scripts/install-sweet-ufs.py` | 把驱动和 root/rootrw/unsealed/rename-swap 修改以幂等方式装入 XNU 源树 |
| `scripts/postprocess-ios-adt.py` | 在实机 FDT 生成的 ADT 上加入 D22AP `/product`、安全属性和 `fuxi-root-extents` |
| `scripts/build-iosroot-nx741j.sh` | 从原始 IPSW 构建、修复、挂载检查并发布根卷 |
| `scripts/check-sweet-root.py` | ADT、extent、内核符号、根镜像和 manifest 的整链主机闸门 |
| `scripts/write-iosroot-device.ps1` | 只读预检后，经 `pinfile` 和显式 `/dev/block/sda12` 写入并回读哈希 |

HFS+ 实现来自原始 iPhone X kernelcache 中预链接的 Apple `HFS.kext`
`com.apple.filesystems.hfs.kext`，版本 `627.100.6`。这正是 data-a-live
`0026-stage-hfs.sh` 固定的 Apple HFS 版本；本机继续使用 Apple 已签名的原驱动，
避免再链接一份同名 VFS 造成重复注册。内核字符串中已核对 HFS bundle、
`hfs_mounthfsplus` 和 `627.100.6`。

启动参数已改为：

```text
native-ufs-root=0 sheng-pmgr=2
rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1
```

旧的 `ShengUfs`、`VirtioMmioBlock`、`VerifiedRoot` 和 `ArmSmmu4K` 组件不再编入
这条启动路径；`native-ufs-root=0` 是明确关闭旧 NativeUfs personality。

## 已验证

- 实机 DTB 与板级声明：`10 claimed nodes, 0 differences`。
- ADT：D22AP / iPhone10,3，D22AP product-id，旧 APFS `root-matching` 已移除，
  39 段 extent 与 JSON 完全一致，phandle 唯一。
- `check-sweet-root.py`：`21 checks, 0 failed`。
- `check-bringup.py`：`16 checks, 0 failed`。
- 内核 SHA-256：`006b4de733475688ea052e62bb693b11d9b3176e39a694c9c26562e13e037164`。
- Payload SHA-256：`36aa6a00c8760ab1ae44b94fb79e7f00dd0eac3938d42401aef1862cd6eb1ed9`。
- ADT SHA-256：`a6cfd4945bb0246e69b553c6ab524ab0a63a9c44065e8fa71e2515c14a3cb4d1`。
- Boot image SHA-256：`6757294804e1f158c8c8bd8c9e177fff99292afb4c9d8552a2e1e4d44cb2fa96`。
- QEMU 16 GiB、8C/16T：`BootEntry`、`MARK #29`、`EBS ok`，`panic=0`。

## 真机剩余步骤

当前 Windows 没有枚举 Android/ADB、fastboot 或 9008，因此不能执行第 4 步的 raw
写入和第 5 步的真机启动。设备重新连接 Android 后按顺序执行：

```powershell
# 1. 只读检查：pinned、39 段未变化、首尾 raw 块为空
.\scripts\write-iosroot-device.ps1 `
  -Image E:\default-workspace\z80u-collect\iosroot-artifacts\iosroot-NX741J-20H392-20261122.img `
  -ExtentsJson E:\default-workspace\z80u-collect\_tmp\nx741j-iosroot-extents-20261122.json

# 2. 显式指定 NX741J userdata，流式写 16 GiB 后做全量 raw SHA-256 与 HFSX 回读
.\scripts\write-iosroot-device.ps1 `
  -Image E:\default-workspace\z80u-collect\iosroot-artifacts\iosroot-NX741J-20H392-20261122.img `
  -ExtentsJson E:\default-workspace\z80u-collect\_tmp\nx741j-iosroot-extents-20261122.json `
  -Write
```

写入脚本固定使用：

```text
pinfile /data/local/tmp/iosroot.img /dev/block/sda12
```

不使用默认 `by-name/userdata`，不重分区，不清 Android 数据。

真机目标日志顺序：

```text
SMMU stream 0x60 -> bypass
root file has 39 extents
START STOP UNIT ACTIVE
ufsroot0
HFSX mount
IOFindBSDRoot
launchd PID 1
```

在观察到这些结果前，不能声称 iOS 已在真机启动。显示、触摸、按键和 GPU 继续保持
冻结。
