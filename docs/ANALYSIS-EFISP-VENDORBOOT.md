# 物证分析：efisp 引导槽、vendor_boot 载体与工具箱免解 BL

分析对象：

- `E:\default-workspace\z80u-collect\NX741_unlock\`
- `E:\default-workspace\z80u-collect\vendor_boot_unpack\`
- `E:\Nubia_Flash_Brick\backup\ZTEToolBoxParBak_2026.10.24_22.38.43.421\images\efisp.img`
- `E:\Nubia_Flash_Brick\中兴家族工具箱1.2.8-beta4\`（免解 BL 相关脚本与 8E5 gbl）

分析日期：2026-11-07。

## 0. 结论摘要

1. **`efisp` 是 3 MiB 的单一 PE 槽**，官方内容是 ZTE 版 Qualcomm **LinuxLoader**（锁定态）。ABL 通过 GBL 漏洞加载它，`bin/conf/dev-NX741J.bat` 里 `blplan=efisp` 说明这是 NX741J 官方认可的"自定义引导槽"。
2. 我们看到的 fastboot **就是 efisp 里的 LinuxLoader**：`flash:` / `flashing unlock` / `getvar:` / `oem ...` 命令齐全，但带 `Flashing is locked` 门槛，锁定态下拒绝写入，表现为 `remote: 'unknown command'`。
3. `NX741_unlock/linuxloader_unlock.efi`（40,960 B）与工具箱 `gbl_unlock.efi` **sha 完全相同**（`88918dd2…`），真实身份是 `QcomTestPkg/VbRwStateApp`——只改 VbRW 状态的小应用，**不是可刷写的 loader**。
4. 工具箱"免解 BL"= 把 200,704 B 的改装 LinuxLoader（`gbl_superfastboot.efi`）写进 efisp，让 ABL 从 efisp 而非官方路径引导；清空 efisp 用 3 MiB 全零镜像。
5. `vendor_boot` 的 vendor ramdisk 是 **LZ4 帧 → cpio**（341 个条目：338 个 `lib/modules/*.ko` + `first_stage_ramdisk/fstab.qcom`）；`dtb` 是 **15 个 DTB 的合并表**（首 blob 495,884 B，总 6,942,916 B）。
6. `boot.img` v4 头部**没有地址字段**，ramdisk 只能紧跟 kernel（0x80158000 起）⇒ 64 MiB payload 必然压到 0x81F00000 的 DTB 区和 0x81000000 的 vendor ramdisk 区。**这是所有"boot.img 带 ramdisk"镜像失败的根因**，与 ABL 是否解锁无关。

## 1. `NX741_unlock/` 拆解

### 1.1 文件指纹

| 文件 | sha256 前 16 | 说明 |
|---|---|---|
| `edl引导/multi_image.mbn` | `373fca23632eabda` | Sahara 映像 37 |
| `edl引导/multi_image_qti.mbn` | `a98c7ae167f5d0da` | Sahara 映像 36 |
| `edl引导/prog_firehose_ddr.elf` | `0fcf76d0b170e611` | devprg 编程器（与 `_tmp/edlbf` 同一份） |
| `edl引导/qsahara_device_programmer.xml` | `433749ae084aa0f1` | |
| `edl引导/sequencer_ram.elf` | `d7dc67ddd77332e5` | 映像 59 |
| `edl引导/signed_firmware_soc_view.elf` | `3d7d626958aedfdb` | 映像 60 |
| `edl引导/tme_config.elf` | `9080d84be39e4496` | 映像 61 |
| `edl引导/xbl_config_devprg.elf` | `82e901c8235f7521` | 映像 38 |
| `edl引导/xbl_sc.elf` | `228b94ff607834b0` | 映像 21 |
| `linuxloader_unlock.efi` | `88918dd212fefe9d` | = `gbl_unlock.efi` |

### 1.2 `linuxloader_unlock.efi` 是什么

- PE32+ / ARM64 / 3 段：`.text` 28,672 B、`.data` 4,096 B、`.reloc` 4,096 B，共 40,960 B
- strings 全部指向 `QcomPkg/QcomTestPkg/VbRwStateApp`（`DEBUG/gbl_efi.dll`）
- 语义字符串：`Prodmode HOB not found`、`Already started`、`Not Ready`、`Warning Write Failure`、`Write Protected`

⇒ 它是 **VbRW（Verified Boot R/W state）写入器**：在 prod mode 下把 verified-boot 状态写成解锁/上锁。它**不实现 fastboot**，所以不能靠它获得刷写能力。

## 2. `efisp.img` 拆解

- 3,145,728 B，`MZ`→PE@0xE58，machine `0xaa64`，subsystem 10（EFI 应用），entry `0x1000`，image base 0
- 段表：`.text` 94,208 B、`.data` 98,304 B、`.reloc` 4,096 B（其余 2.9 MB 为分区填充）
- 身份字符串：`QcomModulePkg/Application/LinuxLoader/LinuxLoader/...LinuxLoader.dll`，`LinuxLoader`
- 完整 fastboot 命令面：

```
flash:            flashing unlock / unlock_critical / lock / lock_critical
flashing get_unlock_ability      getvar:      erase:      reboot
reboot-bootloader / reboot-fastboot / reboot-recovery      boot-fastboot  boot-recovery
partition-size:   partition-type:    slot-count / slot-successful / slot-unbootable ...
oem device-info   oem select-display-panel   oem set-gpu-preemption
oem set-hw-fence-value   oem audio-framework
```

- 锁判定字符串：`Flashing is locked`、`Flashing Unlock is not allowed`、`Device unlocked: %a`、`get_unlock_ability: %d`
- 无 `ramdisk` / `gzip` / `lz4` / `cpio` / `avb` 字符串 ⇒ 该应用只负责 fastboot 命令面，内核装载由 ABL 侧完成

⇒ `fastboot flash boot_a` 回 `unknown command` 不是协议不支持，而是**锁定态下 `flash:` 分支直接拒绝**。

## 3. `vendor_boot_unpack/` 拆解

### 3.1 vendor ramdisk 的真实格式

| 文件 | 事实 |
|---|---|
| `vendor_ramdisk.gz` | LZ4 帧（magic `02 21 4c 18`，FLG `0x08` 含 content size）→ cpio（`070701`）；压缩 8,802,508 B = stock 头 `vendor_ramdisk_size`；解压 31,204,732 B |
| `vendor_ramdisk_new.gz` | 11,498,662 B，同样 LZ4→cpio |

cpio 条目对比（逐条 SHA-256 + mtime）：

```
entries = 341（338 个 lib/modules/*.ko + first_stage_ramdisk/fstab.qcom + 目录/TRAILER）
内容不同 = 0，内容相同 = 341，mtime 不同 = 0
```

⇒ `vendor_ramdisk_new` **只是重新打包**（cpio 头字段如 inode 变化），没有改任何文件内容。说明上一手确实重打过 vendor_boot，但内容未变。

### 3.2 dtb 是合并表

- 文件 6,942,916 B（= stock 头 `dtb_size`）
- 内含 **15 个 FDT**，首 8 个偏移：`0, 0x7910C, 0xEFDAB, 0x16B06D, 0x1E1FBB, 0x24E0DB, 0x2C81C1, 0x36FC44`
- 各 blob totalsize ≈ 486–505 KB（首个 495,884 B）

⇒ ABL 按平台 id 从中选表，所以我们自制镜像必须原样携带这份表，不能用单板 DTB 替代（已按此修正）。

### 3.3 stock vendor_boot 头部几何

```
header_size=2128  page_size=4096
kernel_addr=0x8000   ramdisk_addr=0x1000000   tags_addr=0x100   dtb_addr=0x1f00000
cmdline: video=vfb:640x400,bpp=32,memsize=3072000 nosoftlockup console=ttynull
         qcom_geni_serial.con_enabled=0 bootconfig
vendor_ramdisk_size=8802508   dtb_size=6942916
```

布局：header 4096 → ramdisk 4096..8,806,604 → dtb 8,810,496..15,753,412（ramdisk 尾到 dtb 之间只有 6.4 MB 余量）。

### 3.4 fstab 暴露的分区用途

`first_stage_ramdisk/fstab.qcom` 里 `boot / init_boot / vendor_boot / dtbo / recovery / metadata` 全部按 `slotselect,first_stage_mount` 挂载 —— 证实这台机器是标准 GKI 布局：**boot 无 ramdisk，init_boot 放通用 ramdisk，vendor_boot 放 vendor ramdisk**。

## 4. 工具箱"免解 BL"拆解

### 4.1 设备配置 `bin/conf/dev-NX741J.bat`

```
model=努比亚Z80Ultra   cpu=骁龙8E5   storagetype=ufs
blplan=efisp           blplan_frp=y  parlayout=ab_rec  bootpar=init_boot
presskeytoedl=n        edlconfigureplan=zte
```

**`blplan=efisp`**：本机型的解锁/免解全部围绕 efisp 分区；`edlconfigureplan=zte` 正是我们裸写流程里 `configure ... Oem="ZTE"` 的来源。

### 4.2 8E5 gbl 三个程序

| 文件 | 大小 | sha256 前 16 | 身份 |
|---|---|---|---|
| `gbl_superfastboot.efi` | 200,704 | `be36de1232a1f578` | 改装版 LinuxLoader（release），来自 `SuperTurtleDev/gbl_root_canoe`，标称 superfastboot 3.1-beta |
| `gbl_unlock.efi` | 40,960 | `88918dd212fefe9d` | VbRwStateApp（= `NX741_unlock/linuxloader_unlock.efi`） |
| `gbl_lock.efi` | 40,960 | `c2885c35541d316b` | VbRwStateApp 上锁版 |
| `efisp_empty.img` | 3,145,728 | `bbd05cf6097ac9b1` | 3 MiB 全零（实测 nonzero bytes = 0） |

`gbl_superfastboot` 的 strings 是 stock efisp 的**子集**（无独有字符串），说明 patch 在代码层而非新增功能字符串。

### 4.3 脚本里所有写 efisp 的位置

| 行号 | 动作 | 用途 |
|---|---|---|
| 150 | 写 `efisp_empty.img` | 菜单 19「清空 efisp 分区(去除开机小字)」 |
| 179 | 写 `gbl_superfastboot.efi` | 菜单 18「骁龙8E5免解BL+保指纹」 |
| 757 | 写 `gbl_lock.efi` | 上锁流程 |
| 767 | 写 `efisp_empty.img` | 上锁后清理 |
| 1061 | 写 `gbl_unlock.efi` | 解锁流程 |
| 1077 | 写 `efisp_empty.img` | 解锁后清理 |

### 4.4 三条流程的真实顺序

**解锁（UNLOCKBL-EFISP）**

1. 9008 → `write qcedl efisp gbl_unlock.efi`
2. 重启 → ABL 从 efisp 加载 VbRwStateApp → 写 VbRW 状态
3. 进 fastboot → `fastboot flashing unlock` / `unlock_critical`
4. `write qcedl efisp efisp_empty.img` 清空

**上锁（LOCKBL-EFISP）**：写 `gbl_lock.efi` → 重启 → `fastboot flashing lock` → 清空 efisp

**免解 BL（8E5CUSTOMIZEDBL）**

1. 9008 → `write qcedl efisp gbl_superfastboot.efi`
2. 重启，由 efisp 中的改装 LinuxLoader 接管引导（"从 efisp 而不是官方 abl 引导启动"）
3. 未解锁也能刷第三方；已解锁可保指纹
4. 需要还原时写 `efisp_empty.img`

**恢复（UNLOCKBL-EFISP-1，解锁失败设备异常时）**：恢复 `abl_%slot%` 备份 → 清空 efisp → 重启。

工具箱明确警告："若当前系统已修复 efisp 漏洞，本功能将无效"。

### 4.5 与上游 `gbl_root_canoe` 的差别

上游项目写进 efisp 的是 **BDS.efi**（superfastboot BDS，桥接 `BOOTENTRIES`，支持 `fastboot boot xxx.efi`）；工具箱分发的是 **改装 LinuxLoader**，两者不是同一个二进制。因此不能假定工具箱这份具备 `fastboot boot`。

## 5. 现象与物证的对应关系

| 现场现象 | 物证解释 |
|---|---|
| `fastboot flash boot_a` → `remote: 'unknown command'` | efisp 里的 stock LinuxLoader 处于锁定态，`flash:` 分支被 `Flashing is locked` 拒绝 |
| boot.img 带 64 MiB ramdisk（无论有无追加 DTB）→ 掉 fastboot | v4 头无地址字段，ramdisk 固定落 0x80158000 起 64 MiB，压穿 0x81F00000 DTB 区与 0x81000000 vendor ramdisk 区 |
| vendor_boot 换 64 MiB FAT payload（stock 地址 / 高地址均试）→ 掉 fastboot | 装载器对 vendor ramdisk 有格式/尺寸/地址约束，FAT 原始镜像不满足；具体约束需控制组实验区分 |
| boot.img 不带 ramdisk → 正常进 UEFI | 无区域冲突，装载路径最短 |

## 6. 对 UniXNU 移植的四条可选路径

| 路径 | 做法 | 优点 | 风险/代价 |
|---|---|---|---|
| **A. efisp 放自研 loader** | 把自有 EFI 应用写进 efisp（官方支持的槽位），由它读分区取 payload 或直接 chainload FV | 不依赖 boot/vendor_boot 布局限制，链路最短 | 需自写 EFI 程序；必须处理 GBL 加载约定（PE 起始、分区 3 MiB） |
| **B. payload 缩到 ≤13.6 MB 走 boot ramdisk** | 压缩/裁剪内核 | 沿用标准流程 | 不可行：kernel.macho 49.3 MB 全是有效段（`__PLK_TEXT_EXEC` 22.8 MB 等），无 DWARF 可剥 |
| **C. 固件侧读 UFS 分区** | payload 放 persist/userdata，固件启动后用 BlockIo/FS 读出 | 彻底绕开装载器限制，payload 可任意大 | 需要 UFS DXE 驱动与文件系统栈（当前 layer 2 未过 RpmhDxe） |
| **D. 装 `gbl_superfastboot` 拿可刷 fastboot** | 写 efisp 换掉锁定 LinuxLoader | 立刻获得 `fastboot flash`（免解） | 只解决"能不能刷"，不解决 ramdisk 布局限制 |

建议顺序：先做一次**控制组实验**（vendor_boot 换回 stock 原件）区分"装载器拒绝我们改的 vendor_boot"还是"拒绝所有非 stock vendor_boot"；随后按结论选 A 或 C。D 可作为刷机手段的补充。

## 7. 关键文件清单

| 用途 | 路径 |
|---|---|
| stock vendor_boot 原件 | `E:\Nubia_Flash_Brick\backup\ZTEToolBoxParBak_2026.10.24_22.38.43.421\images\vendor_boot_a.img` |
| stock efisp（LinuxLoader） | 同上 `efisp.img`（sha `4df18359…`） |
| 免解 BL gbl | `…\中兴家族工具箱1.2.8-beta4\bin\tool\Other\8e5gbl\gbl_superfastboot.efi` |
| 解锁/上锁 VbRW | 同上 `gbl_unlock.efi` / `gbl_lock.efi` |
| 清空 efisp | 同上 `efisp_empty.img` |
| 设备配置（blplan） | `…\bin\conf\dev-NX741J.bat` |
| vendor_boot 拆包产物 | `E:\default-workspace\z80u-collect\vendor_boot_unpack\` |

---

# 追加分析（2026-11-07 11:40）：efisp 实测回读与 14 MB ramdisk 窗口

## 8. efisp 实测：**不是空的**，就是 stock LinuxLoader

用 9008 裸读 efisp（LBA 748894，768 扇区）得到：

```
efisp-current.img  3,145,728 B  sha256 4df183594a63af28...
首 16 字节 = 4d 5a 00 00 ...（MZ）
```

与 10.24 备份的 `efisp.img` **完全同 hash**。因此：

- 之前"解锁流程清空了 efisp"的推断**错误**（工具箱的 `efisp_empty` 写入只发生在菜单 19 或解锁/上锁的清理步骤，本机未走到）；
- 救砖包 `flash_all.bat` 里 efisp 那行是 `@REM` 注释，也不会清它；
- 设备上确实还留着官方 LinuxLoader。

## 9. 为什么 unlocked 之后 `fastboot flash` 仍然 `unknown command`

`fastboot getvar` 实测：

```
unlocked: yes          current-slot: a
(bootloader) Device unlocked: true
(bootloader) Device critical unlocked: true
(bootloader) Verity mode: true
```

而 `fastboot flash boot_a`：

```
Sending 'boot_a' (67776 KB)   OKAY
Writing 'boot_a'              FAILED (remote: 'unknown command')
```

⇒ 现在连的这个 fastboot **不是 efisp 里的 LinuxLoader**，而是 boot 失败后 **ABL 自带的最小 fastboot**（只实现 `getvar`/`oem device-info`）。
`efisp` 中的 LinuxLoader 是"正常引导"用的应用；它一旦装载失败，ABL 接管并显示最小 fastboot。

## 10. ramdisk 只有 14 MB 窗口（关键约束）

stock vendor_boot 头与 LinuxLoader 的固定布局：

| 区域 | 地址 | 来源 |
|---|---|---|
| kernel | 0x80008000 | boot 约定 |
| ramdisk | **0x81000000** | vendor_boot 头 `ramdisk_addr=0x1000000` + 基址；LinuxLoader 固定常量 |
| tags | 0x81E00000 | 固定 |
| dtb | 0x81F00000 | 固定（多 DTB 表 6,942,916 B 从这里起） |

⇒ ramdisk 可用区间 = `0x81000000 .. 0x81E00000` = **14 MB**（留出 tags 前后安全余量则更小）。

这解释了三件事：

1. 64 MiB payload 作为 **boot.img ramdisk** 一定失败（v4 头无地址字段，装载器把它放 0x81000000，直接压穿 tags/dtb）；
2. 作为 **vendor_boot vendor ramdisk** 也失败，因为 LinuxLoader 用自身常量而**不读** vendor_boot 头的 `ramdisk_addr`（高地址版无效，实测两次结果一致）；
3. 该约束与是否解锁**无关**（unlocked 后 flash 仍被最小 fastboot 拒绝，ramdisk 布局也照旧）。

## 11. 固件侧存储能力盘点（路径 C 的依据）

现有 FDF/APRIORI 已包含：

```
DXE.inc:  DiskIoDxe, PartitionDxe, FatPkg/EnhancedFatDxe
```

缺的是 UFS 主机控制器驱动，而固件树里**有现成的二进制**：

```
Binaries/nx741j/QcomPkg/Drivers/UFSDxe/UFSDxe.efi   159,744 B
Binaries/nx741j/QcomPkg/Drivers/UFSDxe/UFSDxe.inf    MODULE_TYPE=DXE_DRIVER, Depex=TRUE
```

⇒ 只要把 `{uefi}/Binaries/nx741j` 加入 `[firmware] packages`，并在 FDF 里 `INF QcomPkg/Drivers/UFSDxe/UFSDxe.inf`，固件就能得到 UFS BlockIo；配合已有的 PartitionDxe/Fat，可以直接从 **userdata**（或其他分区）挂载我们的 FAT payload 卷。

这条路完全绕开"ramdisk ≤14 MB"的限制，payload 可以保持 64 MiB。

## 12. 更新的路径排序

| 路径 | 结论 |
|---|---|
| boot ramdisk 投放 | ❌ 14 MB 窗口，49.3 MB 内核放不下 |
| vendor_boot ramdisk 投放 | ❌ 装载器不读头部地址，同样 14 MB 窗口 |
| **C. 固件加 UFSDxe + payload 放 userdata** | ⭐ 最可行：只改 FDF/board.toml，payload 不受尺寸限制 |
| A. efisp 自研 loader | 可行但工作量大（需自写 EFI 应用并处理 FDT 交接） |
| D. 装 gbl_superfastboot 换可刷 fastboot | 只能解决刷机通道，不解决 ramdisk 尺寸 |

### C 的实施步骤（待做）

1. `board.toml [firmware] packages` 追加 `"{uefi}/Binaries/nx741j"`；
2. `nx741j.fdf` 的 FV 中加入 `INF QcomPkg/Drivers/UFSDxe/UFSDxe.inf`（放在 DiskIoDxe 之前）；
3. `./unixnu build firmware`，确认 FD 里出现 UFSDxe，并保留 4 MiB 尺寸；
4. 9008 把 `payload.img`（64 MiB FAT）写到 `userdata` 起始 LBA；
5. 启动后屏幕日志应出现 UFS 初始化与分区枚举；若 payload 卷被挂载，UniXNU 菜单应出现 `xnu` 项。

### C 的风险

- `UFSDxe.efi` 是 Qualcomm ABL 的二进制，可能依赖 ABL 专有协议/PCD（SCM、UFS 配置表）；Mu-Silicium 环境下能否满足依赖需实测；
- layer 2 尚未过 RpmhDxe（时钟/电源），UFS 控制器可能无法上电；
- 写 userdata 起始会破坏该分区数据（设备已解锁，数据可弃）。

---

# 更正（2026-11-07 11:55）：14 MB ramdisk 窗口只是假设，未经证实

## 13. 这条结论必须降级

第 10 节把"ramdisk 只有 14 MB 窗口"写成了结论，并据此判定"ramdisk 路线作废"。复核后这两条都**不成立**：

### 13.1 决定性反证

11:42 的控制实验：`boot_a` = 已验证能起的无 ramdisk 镜像（`boot-nx741j-fv7d82.img`，sha `a983c154…`），
`vendor_boot_a` = **整分区恢复的原厂原件**（`vendor_boot_a.img`，sha `fabb1a67…`，24,576 扇区全写）。
结果**仍然进入 fastboot**。

原厂 vendor_boot + 已验证 boot_a 都起不来 ⇒ 失败与"ramdisk 内容/尺寸"无关。

### 13.2 污染时间线

- 11:02：`boot_a`（无 ramdisk）正常起 UEFI，屏幕日志到 BDS/SmemStubDxe —— 当时 slot 正常。
- 11:24 起：带 ramdisk / 改 vendor_boot 的每次尝试都掉 fastboot，**表现完全一致**。
- 11:46 才通过 `fastboot getvar` 发现 `slot-unbootable:a: yes`、`slot-retry-count:a: 6`。

也就是说 11:24–11:46 之间的全部实验都被 slot 标志污染，不能用来推断装载器对 ramdisk 的处理。

### 13.3 14 MB 窗口的真实状态

它只是从 stock 头部推出来的**静态布局**：

```
ramdisk_addr 0x1000000 -> 0x81000000
tags_addr             -> 0x81E00000
dtb_addr 0x1f00000    -> 0x81F00000
```

装载器完全可能：按 ramdisk 实际大小重排 tags/dtb、或把 ramdisk 放到别处、或直接报错——**三种都未被观测**。

## 14. 修复 slot 后的第一优先实验

1. 9008 全量恢复（`restore-9008.ps1`，10.24 备份 22.3 GiB）→ 确认 `getvar slot-unbootable:a` 变 `no`；
2. **第一个测**：`boot.img` = gzip(shim+FV) + 64 MiB payload ramdisk、无追加 DTB（`boot-nx741j-ramdisk-nodtb.img`，sha `31f5cafc…`）；
3. 若失败，**立刻**再读 `slot-unbootable` / `retry-count`：
   - 仍为 no ⇒ 失败确实来自装载器对 64 MiB ramdisk 的处理，此时 14 MB 窗口才有资格成为结论；
   - 又变 yes ⇒ 只是本次引导失败被计数，需先清标志再重试，不能把失败归因于 ramdisk。

## 15. 现阶段对 payload 投放的正确表述

| 方案 | 状态 |
|---|---|
| boot.img ramdisk（64 MiB） | **未验证**（此前失败被 slot 标志污染），修复后优先重测 |
| vendor_boot vendor ramdisk（64 MiB / 高地址） | **未验证**，同一次污染，需重测 |
| 固件读 userdata（UFSDxe 已在 FV） | 未验证；UFSDxe 属 APRIORI 早加载，是否成功枚举需看屏幕日志 |
| efisp 自研 loader | 可行但工作量大 |
| boot.img 内 EFI 镜像（LinuxLoader 支持 `Booting EFI image`） | 新线索，未验证 |

## 16. 2026-11-07 下午的实机结论（对第 15 节的更新）

### 16.1 boot.img 带宽 ramdisk：干净复测后**被证伪**

前置条件这次是干净的：`fastboot getvar slot-unbootable:a` = `no`、`slot-successful:a` = `yes`、
`slot-retry-count:a` = 6（13:19 写入前确认）。

- 镜像：`boot.img` = gzip(BootShim+FV) + 追加 DTB + **64 MiB payload ramdisk**，
  sha256 `a2949fd034d687192aafb1eb70fb5923248a61a86ee5f4dcdd7182651506d73b`，69,402,624 B / 16,944 扇区；
- 写入：9008 裸写 `boot_a`（LBA 119078），读回校验一致，`power reset`；
- 结果：复位后约 **4 秒**（13:19:41）设备出现在 **fastboot**（`VID_18D1:PID_D00D`），
  不是崩溃、不是 dump —— 装载器判定镜像不合格后回退 fastboot；
- 随后 `fastboot getvar slot-unbootable:a` 变回 `yes`：一次失败就置位。

因此"boot.img 带 64 MiB ramdisk"这条路**确定不可用**。14 MB 窗口的精确上限仍未二分测量，
但内核本身 49.3 MiB（`__PLK_TEXT_EXEC` 22.83 + `__PRELINK_TEXT` 7.77 + `__TEXT_EXEC` 7.55），
即使上限真是 14 MB 也装不下，**ramdisk 路线整体不成立**。

### 16.2 boot.img 内 EFI（`MZ`）路径：三次实验全部失败

反汇编确认 stock LinuxLoader（efisp）里有这样一段：镜像 ≥ 0x660、头 8 字节 `ANDROID!`、
header_version≥3 用 4096 页、读 `kernel_size`、校验 `MZ`、打印 `Booting EFI image...`、
然后 `LoadImage`(BS+0xC8) + `StartImage`(BS+0xD0)。

用三个自建 PE 做实验（都放在 boot.img 的 kernel 区，`MZ` 在偏移 4096）：

| 时间 | 探针 | 行为 | 结果 |
|---|---|---|---|
| 12:49 | `HelloPayload.efi` 19,968 B（无 `.reloc`） | ConOut 打印后返回 | ~23 s 掉 `QUSB_BULK` dump |
| 13:12 | `probe.efi` 3,072 B（有 `.reloc`） | ConOut 打印后死循环 | ~17 s 掉 dump |
| 13:16 | `probe2.efi` 2,048 B（有 `.reloc`，纯忙循环、不调用任何 UEFI 服务） | 什么都不做 | ~22 s 掉 dump |

第三次最关键：探针**不碰任何 UEFI 服务**仍然掉 dump，说明不是探针崩溃，而是
**`MZ` 分支根本没有被执行**（或 `LoadImage` 在 `StartImage` 之前就返回错误）。
该函数存在，但显然还有未找到的触发条件（特定 boot 模式/标志）。**此路径放弃**。

### 16.3 唯一被证实会执行自定义代码的槽位：efisp

- 官方 `blplan=efisp`（`bin/conf/dev-NX741J.bat`），efisp 分区 3 MiB（LBA 748894，768×4096）；
- stock 内容是 3 MiB 的 PE（Qualcomm LinuxLoader，sha `4df18359…`）；
- 工具箱"骁龙8E5免解 BL"（菜单 18）会把 `bin\tool\Other\8e5gbl\gbl_superfastboot.efi`
  写进 efisp，写完后**确实由它接管引导**（12:18 那次 qcedl 日志可见 `write.bat-qcedl parname:efisp`）；
- 结论：把自研 loader 做成 PE 写进 efisp，是当前唯一"确定会执行"的注入点。

### 16.4 metadata 与 userdata 必须成对重建（新发现的硬约束）

13:29 的全量 9008 恢复重写了 `metadata.img`（备份里的旧 FBE 密钥），而 userdata 是 13:06
用新密钥格式化的，于是：

```
vold: Can't mount filesystem /dev/block/mapper/userdata ... f2fs ... invalid argument
init: Failed to mount /data
reboot: Restarting system with command 'volume-failed'
```

ABL 随后带 `kmparam.mode=recovery` 启动 → 无限进 TWRP。`pstore/console-ramoops-0` 可直接读到上述日志。

**修复**：fastbootd 里 `fastboot format:f2fs metadata` + `fastboot format:f2fs userdata`（成对），
首次开机重建密钥后 Android 正常。**任何全量恢复之后都必须做这一步**。

### 16.5 slot-unbootable 的观测

- 每次 boot.img 引导失败都会把 `slot-unbootable:a` 置 `yes`（64 MiB ramdisk 实验后立刻复现）；
- 它不在 `misc`、`devinfo`、`frp`、`ztecfg`、`uefivarstore`、`secdata`、`ssd` 里
  （这些分区与 10.24 备份逐字节一致，设备仍报 `yes`）；
- 13:00 那次写回原厂 boot_a 后，设备进过一次 TWRP，再读已是 `no`——**"成功引导 recovery" 可能清标志**，
  但未做控制实验，不下结论。操作上：失败后先读 `getvar`，必要时重进 9008 或走一次 recovery 再读。

### 16.6 下一步：efisp loader 三段计划

| 段 | 内容 | 产物 |
|---|---|---|
| P1 | 探针只打印标记 + 死循环，验证 efisp 会执行我们的 PE | `efi-probe/efisp-probe.img`，sha `b434047a…`，3 MiB |
| P2 | `BlockIo` → GPT 找 `userdata` → 读 64 MiB payload → 构造最小 FDT | `efi-loader/p2.efi` 6,144 B（4 段含 `.reloc`）；`efisp-p2.img` sha `0901fcf6…` |
| P3 | 读 FV 进内存 → 复制到 `0xFF300000` → 清 cache → 跳 SEC 入口 | 待写；风险：EFI 下 `0xFF300000` 可能只读/保留，需先打印 FD 头再跳 |

P2 的 FDT 已在主机上用 `dtc` 反编译验证：

```dts
/ { compatible = "nx741j"; #address-cells = <2>; #size-cells = <2>;
    chosen { linux,initrd-start = <0x00 0x…>; linux,initrd-end = <0x00 0x…>; }; };
```

回滚方式：把 `10.24备份\images\efisp.img`（sha `4df18359…`）写回 LBA 748894。

## 17. efisp loader 实机成功（Phase 3 / P5，2026-11-07 14:23）

### 17.1 结论

**Mu-Silicium 固件已经从 efisp 自研 loader 成功启动**，屏幕出现完整 DXE 加载日志：

```
SmemStub: Item 404 -> 0xC1838000 (8192 bytes)
loading driver at 0x…: UfsDxe.efi / PmicDxe.efi / UsbConfigDxe.efi / ULogDxe.efi / …
ASSERT EFI_ERROR (Status == Not Found)      ← TzeLoaderDxe.c:1158，已知非致命
loading kernel at 0x0C2108000 EntryPoint=0x0C2109260
Memory Allocation …  C2/C3/C9 HOB
successfully Enabled UFS Cache Coherency
Framework loaded: ImageHandle=C9D2B1B8 ST=C2E1B000 gBS=C2E1D000
Framework: EFI_KERNEL_PROTOCOL Install -> Success
SmemStubDxe entry (BDS=…) ; EFI_SHELL_PROTOCOL Install -> Success
loading driver at 0x0C1746000 EntryPoint=0x0C1747000 DAUS.efi
```

随后设备进入 `ZTE MemoryDump Mode` —— 崩溃点已在固件内部（`DAUS.efi` 之后），
**不在 loader 链路**。

### 17.2 链路与地址约定

| 阶段 | 内容 |
|---|---|
| 载体 | efisp（LUN4，LBA 748894，3 MiB，官方 `blplan=efisp`） |
| 镜像 | `efi-loader/efisp-p5.img`（PE32+ ARM64，subsystem 10，含 `.reloc`，sha `1e8bdf04…`） |
| 数据 | `payload.img` → `rawdump`+0（LUN0 4,743,976，64 MiB）；`SILICIUM_UEFI.fd` → `rawdump`+16,384 扇区（4 MiB） |
| 读盘 | `BlockIo` 枚举 147 个句柄 → 试 bs=4096 → GPT（32×128 B）→ 找 `rawdump`/`userdata` |
| 交接 | FV 复制到 `0xFF300000`（该区间在 ABL 内存映射里是 `EfiConventionalMemory`，105 个描述符里 Type=7、1024 pages） |
| 入口 | FD 首字 `0x14002754` = `b +0x9D50`（SEC `_ModuleEntryPoint`），x0 = FDT |

### 17.3 入口状态的关键（P3/P4/P5 三次实验）

| 版本 | 动作 | 结果 |
|---|---|---|
| P3 | 直接 `br FD_BASE`（EFI 原环境：MMU 开、中断开） | 崩溃 → MemoryDump |
| P4 | 关 MMU/D-cache/I-cache 后跳 | 仍崩溃（约 66 s 后 dump） |
| **P5** | **先 `msr daifset,#0xf` 屏蔽中断 → 换到自己的 32 KiB 栈 → 关 MMU/缓存 → 跳** | ✅ **固件启动** |

当前 EL 实测为 **EL1**（`el 0x1`），SCTLR_EL1 = `0x30d0190d`；Silicium SEC 入口汇编里
`EL1_OR_EL2` 分支支持两者，且自己设置栈，所以 EL1 + 自设栈不是障碍；差别在**中断屏蔽**：
MMU 关掉后若来一个 IRQ，CPU 会走上一阶段的 `VBAR_EL1`（按虚拟地址映射），必然崩。

### 17.4 loader 源码与产物

| 文件 | 说明 |
|---|---|
| `efi-loader/p5.c` | 成功版本（读 rawdump → 读 FV → FDT → 复制 → 屏蔽中断/换栈/关 MMU → 跳） |
| `efi-loader/build-p5.sh` | `clang --target=aarch64-unknown-windows-msvc` + `lld-link /subsystem:efi_application` |
| `efi-loader/p3n.c` | 只复制不跳的验证版（`copy diff words 0x0` 证明复制正确） |
| `efi-loader/p3.c` | 最早的内存映射探测（`fd … t 0x7`） |
| `efi-loader/p6.c` | stub 版（关 MMU 后跳回自写 stub，用于隔离 SEC） |

### 17.5 下一步（固件内部）

1. 抓完整启动日志（屏幕分段拍照或把 Advanced Logger 缓冲区读出）；
2. 定位 `DAUS.efi` 之后崩溃点（DarwinBoot / XNU 入口 / ZTE 私有驱动）；
3. 既然固件可从 efisp 起，后续不再依赖 `boot.img`：`boot_a` 保持原厂即可。

## 18. 固件 bring-up：驱动补全与观测通道（2026-11-07 傍晚）

### 18.1 重新启用 `TzDxeLA`

`Platforms/ZTE/nx741jPkg/Include/DXE.inc` 与 `APRIORI.inc` 里该驱动被标成
`# SKIPPED(debug):`（早期调试留下的注释），已恢复为正常 INF 行：

```
DXE.inc:15      INF Binaries/nx741j/QcomPkg/Drivers/TzDxe/TzDxeLA.inf
APRIORI.inc:31  INF Binaries/nx741j/QcomPkg/Drivers/TzDxe/TzDxeLA.inf
```

构建后 `Guid.xref` 里可见 `f2f8ccd2-6329-44f9-99c5-1dd1fdf42926 TzDxeLA`。

### 18.2 从 stock 恢复 `AndroidDxe`（DTB/DTBO 匹配）

- stock `uefi_a.img` 是 **ELF 容器**，FV 在 0x1000；其 FV 用 **LZMA** 压缩，
  解压后得到两个 FV（`stock-fvimage-3.bin` / `-4.bin`，共 **180** 个 FFS 文件）；
- 其中 GUID `8e3ff05d-38a5-4c2e-98c9-0ecf4b3f8777`（172 KB）含
  `Exact DTB match found. DTBO search is not required` —— 即 **AndroidDxe**（DTB/DTBO 选择逻辑），
  我们的 FV 原本没有它；
- 已提取其 PE32 段（172,032 B，`MZ`）到
  `Binaries/nx741j/QcomPkg/Drivers/AndroidDxe/AndroidDxe.efi` + 同名 INF（GUID 沿用原值），
  并加入 `nx741j.dsc [Components]`、`APRIORI.inc`（HALIOMMU 之前）与 `DXE.inc`；
- 构建确认：`Guid.xref` 出现 `AndroidDxe`。

### 18.3 `DTBExtnProtocol` / `DBProtocol` 的性质

- `HALIOMMUDxe.efi` 里那句 "not found" 是 `Locate DTBExtnProtocol failed: %r`；
- 把 stock 全部 180 个驱动扫描一遍，**stock 自己也有 25 个驱动打印同一句**
  → 该协议在此平台上本就可能是可选的，这些日志**不足以解释崩溃**；
- `DBProtocol` 字样在 stock 驱动里完全不存在，它来自 ZTE 私有 `TzeLoaderDxe`
  （`ASSERT TzeLoaderDxe.c:1158` 已知非致命）。

### 18.4 观测通道：Mu 的句柄枚举不可用

为了在崩溃时把屏幕落盘，先后加了两套抓屏：

| 组件 | 位置 | 结果 |
|---|---|---|
| `LogDumpDxe` | `nx741jPkg/Drivers/LogDumpDxe`（Depex 先 `TRUE`，后改 `gEfiBlockIoProtocolGuid`） | 标记块全零，驱动没写出任何东西 |
| `SmemStubDxe` 内嵌抓屏 | `SmemStubDxe.c`（入口 + 1 s 定时器 ×16 槽位） | 屏幕可见 `capture start`、`connect all done (0 handles)`，无 `capture disk=…` |

决定性一行：

```
SmemStubDxe: connect all done (0 handles)
```

即 `gBS->LocateHandleBuffer(AllHandles, NULL, …)` 在 Mu 固件里返回 **0 个句柄**；
同样代码在 ABL 的完整 EDK2 里返回 **147** 个（`efisp loader` 打印 `blockio handles: 0x93`）。
结论：**Mu 精简实现的句柄数据库枚举不可用**，抓屏不能依赖 `LocateHandleBuffer`；
下一版应改用 `gBS->LocateProtocol(&gEfiBlockIoProtocolGuid, …)` 直接取协议实例
（或经 `LocateHandle` 单协议查询），再按 `Media->BlockSize` 做字节偏移换算。

### 18.5 仓库与设备状态

| 项 | 状态 |
|---|---|
| `efi-loader/p5.c` + `efisp-p5.img`（sha `1e8bdf04…`） | 可用，固件能从 efisp 启动 |
| `AndroidDxe`（stock 提取） | 已加入 FDF/DSC（未提交） |
| `TzDxeLA` 恢复 | 已改（未提交） |
| `LogDumpDxe` / `SmemStubDxe` 抓屏代码 | 在位但受句柄枚举限制 |
| 设备 | 原厂 Android，efisp = `4df18359…` |

## 19. 阶段 2 崩溃定位：DALSYSDxe 二分（2026-11-07 晚）

### 19.1 事实

实机日志（§17.1）最后一行是 `loading driver at 0x0C1746000 EntryPoint=0x0C1747000 DAUS.efi`，
紧接着进入 `ZTE MemoryDump Mode`。核对 DXE 清单，名字能对上的只有
`DXE.inc:30` / `APRIORI.inc:18` 的 **`DALSYSDxe`**（DALSYSDxe.inf，Depex `TRUE`），
即崩溃发生在 DALSYSDxe 加载/派发期间。

### 19.2 排除项

| 怀疑 | 结论 |
|---|---|
| DAL 配置 blob 缺失 | stock 固件与我们 FV 中 `DALPROP`/`DALSys` 字符串数量一致（各 2），未发现仅 stock 才有的 DAL 配置 |
| `DTBExtnProtocol`/`DBProtocol` 缺失 | stock 的 25 个驱动也打印同样 not found（§18.3），非崩溃原因 |
| 驱动清单缺 DALSYSDxe | 不缺：`DXE.inc:30`、`APRIORI.inc:18` 都在，`Guid.xref` 可查 |

### 19.3 二分固件（已构建）

| 变体 | 内容 | FD sha256 |
|---|---|---|
| V0 | 当前基线 | `543b989c65a02e552704d03b028bddb11d8f693a1275721a667d56b5dbdd2841` |
| V1 | 从 `DXE.inc`/`APRIORI.inc` 注释掉 `DALSYSDxe` | `3f8fa1efcbc0c498a0354c3a60c98b4a594210e7021f528c3b5fc87151e7db66` |

产物：`E:\default-workspace\z80u-collect\fv-bisect\fd-v{0,1}-*.img`（各 4,194,304 B），
两个 inc 文件已还原（`git checkout`，无残留）。

### 19.4 实机测试步骤（待执行）

1. `adb reboot edl` → 9008；
2. 写 FD 到 `rawdump` LBA **4,760,360**（= 4,743,976 + 16,384，LUN0，1024 个 4 KiB 扇区）；
3. 写 `loader/images/efisp-p5.img` 到 `efisp`（LBA 748,894，LUN4，768 扇区）；
4. 复位后看屏幕：
   - 若崩溃点**后移**（出现新的最后一个驱动）→ DALSYSDxe 确认；
   - 若仍在同一处 → 崩溃在更早的依赖或加载器环境，需换 `p6.c` stub 隔离；
5. 恢复：写回 `fd-v0-baseline.img`，再写 stock `efisp.img`（sha `4df18359…`）。

### 19.5 备选解释

若 V1 仍崩在同一处，说明崩溃不在 DALSYSDxe 本身，而在 **SEC/PEI 交给 DXE 的环境**
（ABL 已跑过一轮 UEFI，内存/ACPI/SMEM 状态与 XBL 冷启动不同）。下一步应改用
`efi-loader/p6.c`（跳回 stub）对比 SEC 前后的 `SCTLR_EL1`、MMU 表与内存描述符，而不是继续删驱动。

### 19.6 二分结果（2026-11-07 21:47）

| 变体 | 内容 | 结果 |
|---|---|---|
| V0 | 基线（含 DALSYSDxe） | 上次实机：`DAUS.efi`（= DALSYSDxe）后进入 `ZTE MemoryDump Mode` |
| V1 | 去掉 DALSYSDxe | **仍然 `ZTE MemoryDump Mode`**，且启动后 30 s 内无任何 USB 枚举 |

结论：**DALSYSDxe 不是崩溃点**（去掉它崩溃依旧）。可能原因：

1. 崩溃发生在 DXE 之前的 SEC/PEI（ABL 已跑过一轮 UEFI，Mu SEC 假设的冷启动环境不成立）；
2. 崩溃在另一个驱动，但屏幕日志在 MemoryDump 前被覆盖，看不到最后一行；
3. DALSYSDxe 只是最后一行日志，真正崩溃在它之后的下一个驱动。

### 19.7 恢复步骤（设备当前在 MemoryDump、无 USB）

1. 长按电源键 15 s 强制断电；
2. 关机状态下按住 **音量加 + 音量减**（或音量减），插入 USB → 9008（COM 端口重现）；
3. 写回 stock `efisp.img`（`E:\default-workspace\z80u-collect\efisp.img`，sha `4df18359…`，LBA 748894/LUN4）
   并复位，设备回到 Android；`rawdump` 里的 V1 FD 不影响 Android（只有 efisp loader 会读它）。

### 19.8 下一步：把"最后一行日志"变成可离线读取

Mu 的句柄枚举不可用（§18.4），屏幕日志又会被 MemoryDump 覆盖，因此需要**不依赖屏幕/BlockIo 的观测**：

1. 在固件里加一个 Depex `TRUE` 的早期 DXE 驱动：每派发一个驱动就把序号/时间戳写到固定 RAM 地址（如 `0x8F000000` 的自定义页）；
2. 改 loader（p7）：启动时先读该 RAM 页；若发现"上次崩溃标记"，先把该页写到 `rawdump` 的留空尾部，再继续正常引导；
3. 设备崩溃→断电重启→loader 读出→9008 读回 `rawdump`，即可离线看到最后派发的驱动序号，无需屏幕。

（MemoryDump 会触发复位，DRAM 内容通常保留到下次上电；loader 在 ABL 之后运行，可先读后写。）

### 19.9 第三轮：内存窗口预留（p7）也无效

假设：ABL 的 memmap 仍把 `0xFF300000` 报成 `EfiConventionalMemory`，Mu 的 DXE 堆可能把它
重新分配、覆盖正在运行的固件镜像。

做法：loader `p7.c` 在拷贝 FD 前调用
`AllocatePages(AllocateAddress, EfiReservedMemoryType, 8 MiB / 4 KiB, &0xFF300000)` 锁定窗口
（`efisp-p7.img` sha `82963542…`），配合 V0 基线 FD 刷入。

结果：**仍然 `ZTE MemoryDump Mode`** → 窗口冲突假设也被否证。

### 19.10 副作用与恢复

二分脚本末尾的 `git checkout -- DXE.inc APRIORI.inc` 把 §18 的固件改动（TzDxeLA、AndroidDxe）
一并还原了。已重新加回：

| 文件 | 行 |
|---|---|
| `DXE.inc` | `15: INF …/TzDxeLA.inf`、`30: INF …/AndroidDxe.inf` |
| `APRIORI.inc` | `22: INF …/AndroidDxe.inf`（HALIOMMU 前）、`32: INF …/TzDxeLA.inf` |

LogDumpDxe/SmemStubDxe 的文件与 DSC/FDF 接线仍在（`git status` 可见），补丁其余部分未丢。

### 19.11 已否证的假设与剩余可能

| 假设 | 结果 |
|---|---|
| DALSYSDxe 自身崩溃 | ❌ V1 去掉后仍 MemoryDump |
| DXE 堆覆盖固件窗口 | ❌ p7 预留 8 MiB 后仍 MemoryDump |
| DAL 配置 blob 缺失 | ❌ stock 与我们的 DALPROP/DALSys 字符串一致 |
| DTBExtn/DBProtocol 缺失 | ❌ stock 同样报错 |

剩余可能（按概率）：

1. **SEC/PEI 环境**：ABL 的 UEFI 已经初始化了 GIC/MMU/UFS/时钟，Mu SEC 在"二次进入"时状态不一致；
2. 某个 **Qualcomm 驱动**在特定硬件状态下崩溃（与具体驱动无关的表象是因为崩溃发生在同一驱动，看似固定）；
3. 我们的 **loader 跳转约定**（MMU/cache off、DAIF 屏蔽、自有栈）与 Mu SEC 的期望仍有细微差异。

下一步必须先拿到"最后一行日志"。屏幕日志在 MemoryDump 前会被覆盖，因此计划：
用一个 Depex `TRUE` 的早期驱动 + 定时器，把心跳写到 **SMEM**（不依赖屏幕/BlockIo），
崩溃后由 loader 在下次启动时读出并显示；若 DRAM 不保留，则改为写 UEFI 变量（`uefivarstore` 持久化）。

### 19.12 19:32 实机日志（V0 基线，无探针）—— 真实崩溃点接近 TzDxeLA

用户提供的完整屏幕日志（2026-11-07 19:32:11）：

```
smemstub item 404 -> 0xC17E7000 (8192 bytes)
loading HWIODxeDriver.efi
loading ChipInfo.efi
loading PlatformInfoDxeDriver.efi
loading AndroidDxe.efi
loading HALIOMMU.efi
  locate DTBExtnProtocol failed: not found
  locate DTBExtnProtocol failed with error not found
  error: image at 000C16ED000 start failed: not found
loading ULogDxe.efi
loading IPCCDxe.efi
loading ~
loading ~
loading TzDxeLA.efi
not found
（随后 ZTE MemoryDump Mode）
```

判读：

| 事实 | 含义 |
|---|---|
| DALSYSDxe、HWIODxe、ChipInfo、PlatformInfo 都正常加载 | 之前"崩在 DALSYSDxe"的判断是**误读**（把 `Dalsys` 行当成最后一行） |
| `AndroidDxe` 加载后报 `error: image … start failed: not found` | 我们加入的 stock 提取驱动启动失败，但 DXE 继续（非致命） |
| HALIOMMU 的两条 DTBExtn 失败 | stock 同样存在，非原因 |
| **最后是 `loading TzDxeLA.efi` → `not found`，之后无任何 driver 行** | TzDxeLA 是我们从 `# SKIPPED(debug)` **恢复**的；它的启动失败之后系统就进入 MemoryDump |

### 19.13 V5：重新跳过 TzDxeLA + 探针移到 APRIORI 末尾

| 项 | 内容 |
|---|---|
| 改动 | `DXE.inc`/`APRIORI.inc` 中 TzDxeLA 恢复为 `# SKIPPED(debug):`；StopDxe 探针移到 APRIORI 最后一行（第 119 行前） |
| FD | `fv-bisect/fd-v5-no-tzdxela-probe-end.img`，sha `2e1a00beac82f6ac39a9ecd346be88987d9f2cea37fb2e6a9c8e883f0ab7f616` |
| 判读 | 若屏幕出现 `NX741J STOP` → APRIORI 全部派发完成，崩溃在 DXE.inc 阶段或之后；若直接 MemoryDump → 崩溃仍在 APRIORI 范围内且与 TzDxeLA 无关 |

### 19.14 可能的根因排序（更新）

1. **TzDxeLA**（最可疑）：stock 明确跳过它；我们恢复后它返回 not found 且随后崩溃。V5 直接验证；
2. `AndroidDxe`（stock 提取）启动失败可能留下半初始化状态；
3. APRIORI 之后的某个 DXE.inc 驱动；
4. SEC/PEI 环境（仍未被排除，但优先级降低）。
