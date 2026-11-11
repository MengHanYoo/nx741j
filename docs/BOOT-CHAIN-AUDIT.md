# 启动链（concepts/boot-chain）对照分析

依据 `https://unixnu.com/zh/concepts/boot-chain`；对照本机实际链路（`docs/BOOT-CHAIN.md`、`BOOT-FIRST.md`、真机/QEMU 记录）。

## 1. bootloader → BootShim（**本机不同**）

| 官方 | 本机 |
|---|---|
| bootloader 读 boot.img：kernel 字段 = gzip(BootShim+FD)，随后是设备树 | **ABL 拒绝第三方 boot.img**，改走 efisp：3 MiB PE 槽 |
| BootShim 以 `FD_BASE`/`FD_SIZE` 汇编，重定位 FV 后进 UEFI | `loader/p7.c` 从 rawdump 读 payload(64 MiB)+FD(4 MiB)，拷到 `0xFF300000`，`daifset`+关 MMU/cache，`br` 进 SEC（x0=FDT） |
| 设备树来自 boot.img 尾部 | FDT 由 loader 现场构造，`/chosen/linux,initrd-start/end` 指向 payload（`p7.c:145 fdt_build`） |

两者职责等价（搬 FV、传 FDT），载体不同；**这是全链唯一的结构性偏差**，已在 `BOOT-FIRST.md` 记录。

## 2. UEFI → UniXNU

| 官方 | 本机 |
|---|---|
| `UniXNUDxe` 注册 `Boot####`（load option `unixnu:firmware`），BDS 启动 | ✅ 菜单正常；Boot#### 变量不可用时用 direct-start 兜底（历史 V68） |
| 扫描卷找 `\EFI\UniXNU\unixnu.toml` | ✅ payload 卷（`UniXNURamDiskDxe`+DiskIo+FAT）里读取 |
| 菜单前：block_slice → `[[driver]]` → ACPI 表 → kit 合并 → 菜单 | 部分：无 block_slice（阶段 6 才用）、无 `[[driver]]`、ACPI 已具备（`AcpiTableDxe`）、设备侧 `kits autoload=false`（DarwinBoot 走 FV 驱动而非 kit） |

## 3. UniXNU → DarwinBoot（**DT 路径不同**）

| 官方 | 本机 |
|---|---|
| 准备 DT：固件 DTB/文件 DTB/无 → 复制+预留 64 KiB → 全局补丁 → 菜单项补丁 → `[entry.chosen]` → 安装为 `EFI_DTB_TABLE` | xnu 条目 `DeviceTree=NULL`：`Boot.c:90` 对 `UniXNUEntryXnu` 明确置空 |
| 加载器收到树、ramdisk、参数、kext | 加载器只收 `KernelPath`/`BootArgs`/`Volume`；**设备树走 ADT sidecar**（`kernel.macho.adt`），不是 DT |

结论：Darwin 路径本就以 ADT 为准（文档 `concepts/payload` 也这么规定），但「UniXNU 构造 DT」这一整段在本机**不参与** xnu 启动，属文档未覆盖的简化。

## 4. DarwinBoot → XNU（✅ 逐条对上）

| 官方检查 | 本机证据 |
|---|---|
| 读 `kernel.macho`，校验 Mach-O（MH_EXECUTE/LC_MAIN/LC_SEGMENT_64/入口在可执行段） | `MARK #1–#8` 全过 |
| 段页对齐、不重叠、256 MiB 窗口内 | `MARK #9–#13`；窗口 `owned=0x10000000` |
| 读 ADT、trustcache，校验 trust cache（计数/偏移/条目/排序） | `MARK #14–#20`（真机 + QEMU） |
| 读 GOP framebuffer | `video base=0`（`no-video` 构型）/ 真机面板地址 |
| 分配连续窗口、拷贝段/ADT/TC/ramdisk、构造 `DARWIN_BOOT_ARGS` | `p12 map`、`p13 EBS`、`ARM64 va=0xFFFFFFF003DC8000 pa=0x481C8000` |
| 退 boot services、关 MMU/cache、EL1 跳入口 | `QEMU: EBS ok, entering the kernel` |

`DARWIN_BOOT_ARGS` 布局（0x28 video 六值、0x60 DT、0x6c 命令行、0x478 实际内存）与我们的 `DarwinBoot.c` 一致（命令行 1024 B、`darwinboot-memory-size` 0x10000000）。

## 5. XNU → 用户态（阶段 5 ✅，阶段 6 阻塞）

| 官方 | 本机 |
|---|---|
| `start.s` → bootstrap 页表 → C 启动 → 平台专家 | ✅（阶段 5 全部通过，`PE_create_console`/`machine_startup` 均确认） |
| 平台专家匹配 ADT 根；要求 `/cpus`、`/arm-io`、`/defaults`、`/options`、CHRP NVRAM proxy | ✅ ADT 由实机 DTB 派生，含全部五者 |
| 发布 CPU nub 与每个 `arm-io` 子节点 | ✅（8 CPU、intc、arm-timer、ufshc） |
| 存储驱动发布 userdata；分区方案发布 `OS`/`SystemOS`；按 UUID 匹配根卷 | ❌ **阻塞**：UFS 首条命令后总线锁死（`docs/UFS-VENDOR-DIFF.md`、`PANDORA-UFS.md`） |
| 挂载根卷并启动 `launchd` | ⬜ 未开始 |

## 失败行为对照

| 阶段 | 官方行为 | 本机 |
|---|---|---|
| bootloader | header 不匹配 → 留在 bootloader | ABL 直接拒绝第三方 boot.img（改用 efisp） |
| 固件 | 无 GOP/输入 → headless/文本 | ✅ `no-video=1`、Blt fallback、`console="auto"` |
| UniXNU | 无配置 → 内置配置 + 可移动加载器 | 有配置，未触发 |
| DarwinBoot | kernel/ADT/TC 无效 → `EFI_COMPROMISED_DATA`，固件继续 | 改为 **Fail 屏无限重绘**（便于拍照取证），不返回菜单 = **刻意偏差** |
| XNU | 缺 provider → panic 报名字 | ✅ 已加 `waitForService` 看门狗（0009） |

## 结论

1. 四次交接中，**2/4 完全符合**；1 的载体（efisp vs BootShim）、3 的 DT 路径（ADT sidecar vs UniXNU 构造 DT）、以及 DarwinBoot 失败时的不返回，是本机三处刻意偏差，均已在仓库文档记录；
2. 第 5 段（XNU→用户态）在「存储发布 userdata」这一步阻塞，UFS 接管序列正在按 pandora 的可用实现调整；
3. 建议补一条：在 `BOOT-CHAIN.md` 里把「本机不经过 UniXNU 的 DT 构造」写明白，避免以后有人以为 xnu 条目会带 `DeviceTree`。
