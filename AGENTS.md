# AGENTS.md - NX741J

## 工作区约定
- 构建产物/日志/临时文件放 E: 盘（E:\default-workspace\z80u-collect）。
- 固件树在 WSL：/opt/work/uefi-sheng-main；设备仓库：repos/UniXNU-nx741j。
- 不自动 git commit；仅在用户明确要求时提交。
- **硬件清单与缺失输入**：`docs/HARDWARE-INVENTORY.md`（每条注明来源；缺失项随测量划掉）。
- 结构化移植记录：`porting.toml` 的 14 个 area（`./unixnu` 的 `porting-record.py check` 校验）。

## OS 基准（2026-11-07 起，硬性）

- **唯一基准镜像：iOS 16.7.16（20H392）**，profile `ios-16.7.16-20H392`；
  IPSW：`E:\default-workspace\ipsw\iPhone10,3,iPhone10,6_16.7.16_20H392_Restore (1).ipsw`
  （`product = iPhone10,3`、`device_class = d221ap`、`board_id = 14`、`chip_id = 32789`）。
- `board.toml` 的 `[board] os_profile` 固定为该目录名；输出目录固定为
  `out/nx741j/ios-16.7.16-20H392/nx741j`。
- **不把官方 iPadOS 17/26/27 的 profile、默认 `--os` 或只对 iPadOS 27 成立的前提当成本机基准**；
  仅在对比实验时显式传 `--os`，默认构建永远走 16.7.16。
- 回移官方代码时，凡遇到"仅 iPadOS 27 可用"的分支（例如 `[payload.runtime] appsync`），
  必须保证 16.7.16 的构建路径不受影响，并在 `docs/CONFIG-MAP.md` 记录例外。
- 组件条目的 `os_abi` 必须包含 `ios-16.7.16-20H392`；本机使用的 ArmCpu、ArmDTPlatform、
  SoftwareAES、GopFramebuffer 已补入该值（`components.toml` 为本地 delta，重新同步官方目录后需复查）。
- 基准构建/验证命令：

  ```sh
  ./unixnu release --no-deps -D ../UniXNU-nx741j
  # 或显式指定
  ./unixnu release --no-deps --os ios-16.7.16-20H392 -D ../UniXNU-nx741j
  ```

## 仓库内容布局（2026-11-07 补充）

| 目录 | 内容 |
|---|---|
| `loader/` | efisp EFI loader 源码（`p5.c` 为可用版）与 `images/efisp-p5.img`（3 MiB） |
| `package/firmware-overlay/` | 板级固件代码（如 `AndroidDxe` 与平台驱动），由 `scripts/apply-firmware-overlay.py` 在构建前复制进 UEFI 树 |
| `patches/firmware/` | 固件平台改动补丁（`TzDxeLA` 恢复、`LogDumpDxe`、`SmemStubDxe` 抓屏、DSC/FDF） |
| `docs/` | 分析、交接、硬件清单、共享支持选择、配置地图、本地术语补充（`GLOSSARY-LOCAL.md`）、启动链对照（`BOOT-CHAIN.md`） |

应用方法见 `package/README.md` 与 `scripts/apply-firmware-overlay.py`。

## 发布位置（2026-11-07 起）

不创建 `UniXNU` 组织仓库；可用且带 deploy key 的是 **`MengHanYoo/nx741j`**，两个仓库以分支发布：

| 内容 | 地址 |
|---|---|
| 设备仓库（本仓库） | `https://github.com/MengHanYoo/nx741j/tree/unixnu-nx741j` |
| 框架仓库（`repos/UniXNU`） | `https://github.com/MengHanYoo/nx741j/tree/unixnu-main` |

推送命令（deploy key 只对该仓库有权限）：

```powershell
$env:GIT_SSH_COMMAND='ssh -i C:/Users/MengHanYooo/.ssh/nx741j_push -o StrictHostKeyChecking=accept-new'
git -C E:\default-workspace\z80u-collect\repos\UniXNU-nx741j remote add private git@github.com:MengHanYoo/nx741j.git
git -C E:\default-workspace\z80u-collect\repos\UniXNU-nx741j push private main:unixnu-nx741j
git -C E:\default-workspace\z80u-collect\repos\UniXNU            push private main:unixnu-main
```

`devices.toml` 不登记本机（官方站点只接受 `UniXNU/<name>`），nx741j 以**社区移植**形式出现在
`docs/devices/`（记录快照随仓库提交）；将来迁入 `UniXNU` 组织时把条目加回并改两处链接即可。

## 下载约定（2026-11-07 起）

**所有需要下载的文件都走 Motrix（其 aria2 引擎）**，下载完成后放到对应目录；GitHub 直连不通时走加速器。

```powershell
# 统一入口（自动：直连 → 加速器，落盘并校验 sha256）
powershell -NoProfile -ExecutionPolicy Bypass `
  -File E:\default-workspace\z80u-collect\wsl\fetch-file.ps1 `
  -Url <url> -OutDir <目标目录> [-Name <文件名>] [-Sha256 <hex>] [-Mirrors @(...)]
```

- aria2 引擎：`C:\Program Files\Motrix\resources\extra\win32\x64\aria2c.exe`；
- 默认加速器前缀：`https://ghfast.top/`、`https://gh-proxy.com/`（对 github.com / raw.githubusercontent.com / codeload.github.com 自动追加）；
- 若使用**自建加速器**，用 `-Mirrors @('https://<自建前缀>/')` 覆盖，并在本文件登记该地址；
- 下载产物必须落到任务对应目录（例如组件源码 → `repos/`，证据文件 → `_tmp/<任务>/`），不要散落在临时目录；
- 需要版本固定时用 `-Sha256` 校验；脚本会打印实际 sha256。

## 刷机约定（2026-11-07 起）

**`boot_a` 一律走 9008**，不再用 adb/root 的 `dd`（dd 只在 Android 可用时作为应急回滚）。

```powershell
# 手机进 9008（音量键 + 插 USB，或 adb reboot edl），然后：
E:\default-workspace\z80u-collect\wsl\flash-boota-9008.ps1 -Img <boot.img> [-NoReboot] [-DryRun]
```

链路（照抄中兴家族工具箱 `bin\write.bat` 的 ZTE 方案，是这台机器上实际成功过的流程）：

1. `9008`（`VID_05C6&PID_9008`）→ `QSaharaServer` 上传 devprg 八件套
   （36/37/21/60/59/61/13/38），成功标志 `Successfully uploaded all images`；
2. `qcedlcmdhelper -p COMx -m xml -f configure.xml`，`configure.xml` 必须带 **`Oem="ZTE"`**；
3. `fh_loader --port=\\.\COMx --memoryname=ufs --search_path=<工作目录> --sendxml=<program.xml> --skip_config --showpercentagecomplete --noprompt`
   —— **`--skip_config` 是关键**：不带它，fh_loader 会先自己发一条不带 `Oem="ZTE"` 的
   `<configure>`，devprg 立刻回 `ERROR: Tool doesn't come from ZTE`（该字符串就在 `prog_firehose_ddr.elf` 里）；
4. `qcedlcmdhelper` 发 `<power value="reset"/>`；可选 `--convertprogram2read` 读回校验 SHA-256。

`program.xml` 形如（boot_a）：

```xml
<data><program SECTOR_SIZE_IN_BYTES="4096" filename="boot_a.img" physical_partition_number="4"
 label="boot_a" start_sector="119078" num_partition_sectors="16944" /></data>
```

- 工具目录：`E:\Nubia_Flash_Brick\中兴家族工具箱1.2.8-beta4\bin\tool\Win`
  （`QSaharaServer.exe`、`qcedlcmdhelper.exe`、`fh_loader.exe`），devprg 八件套在 `_tmp\edlbf`
- Android 可用时的应急回滚：`dd if=<stock boot_a.img> of=/dev/block/by-name/boot_a bs=4096 conv=fsync`
  （stock 图：`E:\Nubia_Flash_Brick\backup\ZTEToolBoxParBak_2026.10.24_22.38.43.421\images\boot_a.img`）
- boot_a 分区 96 MiB（100663296 B）；Android boot image v4，`base=0x80000000`、
  `kernel_offset=0x8000`、`tags_offset=0x1e00000`、`pagesize=4096`
- 已知能起的最简镜像：gzip(shim+FV) + 无追加 DTB + ramdisk 0（1.37 MB / 335–338 扇区）
- 全量救砖：`E:\Nubia_Flash_Brick\努比亚Z80Ultra_9008线刷救砖包_*/…/flash_all.bat`（进 fastboot 后全量刷）

## 已知硬件事实
- SoC：SM8850（canoe/Kaanapali），GPU：Adreno 840（gen80200 固件）。
- 显示：面板 bf375/rm692h5，原生 1440x3200；UEFI GOP 必须为 1216x2688。
- 存储：UFS；boot_a LBA 119078，uefi_a LBA 6，efisp LBA 748894（768 扇区）。
- SMEM：0x81D00000/0x200000，由 XBL 初始化（xbl_config_a.img），ABL 不参与。

## 双系统分区策略
| 方案 | 位置 | 说明 |
|---|---|---|
| A | efisp 分区 | 首选，独立 3 MB |
| B | userdata FAT 镜像 | 备选 |
| C | FV 内嵌 FAT | 自包含但需重建 |

当前选择：先 A。

## 构建命令
./unixnu status --json
./unixnu tree
./unixnu build firmware -D ../UniXNU-nx741j --uefi /opt/work/uefi-sheng-main

## 并行度（2026-11-07 实测）

- 主机：**AMD Ryzen 9 5950X（16C/32T）**，Windows 物理内存 31.9 GiB；WSL `.wslconfig` 为
  `processors=32`、`memory=28GB`、`swap=8GB`，WSL 内 `nproc = 32`。
- 传递链：`unixnu.local.toml` 的 `jobs` → `ctx.jobs`，再传给内核（`JOBS`）、package（EDK2 `-n`）、
  firmware（EDK2 `-n`）、BaseTools（`make -j`）；`--jobs N` / `-j N` 可逐次覆盖。当前 `jobs = 32`。
- 干净内核编译（删除 `tree/build/xnu/ios-obj` 后计时）：

  | 并发 | 耗时 |
  |---|---|
  | 32 jobs（冷缓存） | 202.9 s |
  | 32 jobs（暖缓存） | 209.0 s |
  | 16 jobs（暖缓存） | **148.0 s** |
  | 增量重建（32 jobs） | 28.4 s |

- 结论：**内核编译用 16 jobs 反而比 32 快**（16C/32T 的 SMT 超订拖慢编译）。`jobs = 16` 适合内核；
  EDK2 固件/package 与 QEMU 冒烟可单独传 `-j 32` / `JOBS=32`。QEMU 本身单线程，冒烟耗时由 6 个启动
  场景决定（32 jobs 下 225.7 s，6 场景 + 14 个 kext 子用例全部 PASS）。

## 执行与网络约束（硬性）

- **一切构建、编译、刷机、测试都在本机执行**（Windows + WSL `Ubuntu`，32 vCPU / 27 GB）。
  DeepSeek Harness 只用于思考、编排与记录，不承担任何远程构建/编译。
- **构建过程必须离线可完成**：源码只允许来自本地镜像缓存 `/opt/ghcache`（git `insteadOf`
  已指向该缓存）或已 fetch 的 `out/feeds/`；禁止构建时临时拉取网络资源。
- 需要新源码时，先把仓库缓存进 `/opt/ghcache`（在线一次），再离线构建。
- 固件构建：`bash firmware/smem-stub/build.sh`（EDK2 BaseTools，本地树 `/opt/work/uefi-sheng-main`）。
- 内核构建：`./unixnu build kernel`（本地 WSL，clang 24 + cctools 在 `/opt/unixnu-linux`）。
- 刷机/取数：`wsl/write_img.ps1`、`adb`（本地 USB，不经网络）。

## 环境事实

| 项 | 值 |
|---|---|
| WSL 发行版 | Ubuntu 26.04（`-d Ubuntu`，root） |
| CPU / 内存 | 32 vCPU / 27 GB |
| 工具链 | `/opt/unixnu-linux`（clang 24 + 38 个 Mach-O 工具，2026-11-06 装好） |
| 源码缓存 | `/opt/ghcache/github.com/**`（xnu 186 MB 等 9 个仓库） |
| 固件树 | `/opt/work/uefi-sheng-main` |
| 产物盘 | `E:\default-workspace\z80u-collect`（`out/` 在 E 盘） |
| 平台工具 | `E:\default-workspace\tools\platform-tools`（`adb.exe`/`fastboot.exe` 的 E 盘副本） |

## 2026-11-07 晚间补充（实机验证后）

### 只用 E 盘

- 不读写 C 盘：临时文件落 `E:\default-workspace\z80u-collect\_tmp\...`，
  WSL 侧落 `/mnt/e/default-workspace/z80u-collect/_tmp/wsl-tmp`（**不要用 `/tmp`、`/var/tmp`**，
  它们在 C 盘的 WSL 虚拟磁盘里）。
- `adb`/`fastboot` 用 E 盘副本：`E:\default-workspace\tools\platform-tools\{adb,fastboot}.exe`；
  工具箱 exe 复制到 `_tmp\edl-tools` 后再调用。

### 全量恢复后的硬约束

**任何 9008 全量恢复之后，`metadata` 与 `userdata` 必须成对重建**：

```powershell
fastboot reboot fastboot        # 需要 boot_a 是原厂镜像（AVB 通过）才起得来
fastboot format:f2fs metadata
fastboot format:f2fs userdata
fastboot reboot
```

否则 `metadata` 里的旧 FBE 密钥与 userdata 不匹配 → `vold` 挂载 `/data` 失败 →
`reboot: Restarting system with command 'volume-failed'` → ABL 带 `kmparam.mode=recovery`
无限进 TWRP。判定证据：`/sys/fs/pstore/console-ramoops*`、`/proc/cmdline` 里的 `kmparam.mode`。

### 每次试 boot.img 前后的固定动作

1. 试之前：`fastboot getvar slot-unbootable:a` / `slot-retry-count:a` / `slot-successful:a` 记录下来；
2. 失败后：立刻再读一次——一次引导失败就会把 `slot-unbootable:a` 置 `yes`；
3. 该标志不在 `misc`/`devinfo`/`frp`/`ztecfg`/`uefivarstore`/`secdata`/`ssd` 里（这些分区与备份逐字节一致时仍可能为 `yes`），
   实践中"进一次 recovery 再读"曾观察到它变回 `no`，但未做控制实验；
4. 恢复手段优先级：写回原厂 `boot_a`（9008）→ 全量恢复 → 成对重建 meta/data。

### 已证伪的两条 payload 路线（不要重复投入）

| 路线 | 结论 |
|---|---|
| `boot.img` 带 64 MiB ramdisk | slot 健康时干净复测仍**回退 fastboot**（一次失败即置 `slot-unbootable`），且内核 49.3 MiB 远超 14 MB 窗口，整体不成立 |
| `boot.img` kernel 放 `MZ` PE（`Booting EFI image` 分支） | 三次实验（含**不调用任何 UEFI 服务**的纯忙循环探针）全部在 ~17–23 s 掉 `QUSB_BULK` dump，说明分支未执行或 `LoadImage` 失败，**放弃** |

### 当前唯一注入点：efisp

efisp（3 MiB，LBA 748894）是官方 `blplan`，stock 为 Qualcomm LinuxLoader PE（sha `4df18359…`）；
工具箱"免解 BL"写 `gbl_superfastboot.efi` 进去就能接管引导 → 自研 PE 写进去一定能被执行。

**2026-11-07 已实机成功**：`efi-loader/efisp-p5.img`（sha `1e8bdf04…`）从 efisp 启动 Mu-Silicium 固件，
屏幕出现完整 DXE 日志（UFS/Pmic/Usb/Graphics/Framework/EFI_KERNEL_PROTOCOL），最后才在 `DAUS.efi`
之后进入 MemoryDump —— 属固件内部问题，不是 loader 问题。

loader 链路（详见 `docs/ANALYSIS-EFISP-VENDORBOOT.md` 第 17 节）：

1. `BlockIo` 枚举 → 试 bs=4096 → GPT 找 `rawdump` / `userdata`；
2. `payload.img`（64 MiB）放 `rawdump`+0；`SILICIUM_UEFI.fd`（4 MiB）放 `rawdump`+16,384 扇区；
3. 构造 FDT（`/chosen/linux,initrd-start/end`）；
4. FV 复制到 `0xFF300000`（ABL 内存映射里是 `EfiConventionalMemory`）；
5. 入口状态必须：**`msr daifset,#0xf` 屏蔽中断 → 换自己的栈 → 关 MMU/D-cache/I-cache → `br 0xFF300000`（x0=FDT）**。
   只关 MMU 不屏蔽中断会崩（P4 实验）；当前 EL=1，SCTLR_EL1=`0x30d0190d`。

回滚：把 `10.24备份\images\efisp.img` 写回 LBA 748894。

### Mu 固件的句柄枚举限制（2026-11-07 傍晚发现）

`gBS->LocateHandleBuffer(AllHandles, NULL, …)` 在 Mu 固件里返回 **0 个句柄**
（屏幕证据 `SmemStubDxe: connect all done (0 handles)`），而同一调用在 ABL 的完整 EDK2 里
返回 147 个（efisp loader 打印 `blockio handles: 0x93`）。因此：

- 想在固件里枚举设备/写盘，**不要用 `LocateHandleBuffer`**；
- 改用 `gBS->LocateProtocol(&gEfiBlockIoProtocolGuid, …)` 直接取协议实例，按 `Media->BlockSize` 换算 LBA；
- 抓屏落盘（`LogDumpDxe`、`SmemStubDxe` 内嵌抓屏）目前因这一限制无法工作。

### 固件驱动补全（未提交）

- `TzDxeLA`：`Include/DXE.inc:15`、`Include/APRIORI.inc:31` 已从 `# SKIPPED(debug):` 恢复；
- `AndroidDxe`（stock 提取，GUID `8e3ff05d-38a5-4c2e-98c9-0ecf4b3f8777`，172,032 B）：
  放在 `Binaries/nx741j/QcomPkg/Drivers/AndroidDxe/`，已加入 `nx741j.dsc [Components]` 与 `APRIORI.inc`（HALIOMMU 之前）；
- stock `uefi_a.img` 的 FV 是 **ELF + LZMA**，解压脚本与产物见 `_tmp/stock_fv_lzma.py`、`stock-fvimage-*.bin`（180 个驱动）。
