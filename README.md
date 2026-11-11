# UniXNU-nx741j

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali, codename
`canoe`), 8 cores (6 + 2), Adreno 840, UFS KLUGG8NHKB, panel `bf375`/`rm692h5` 1440×3200.

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned in
[board.toml](board.toml) (`os_profile = "ios-16.7.16-20H392"`); it is the only IPSW available for this
port, so the iPadOS 17 / 10063 line is not a valid substitute.

All build inputs come from the UniXNU project itself (board.toml, components.toml,
patches/xnu/series, the official docs). Other device projects are read for reference only and never
enter the build.

## 本地开发

本机路径与 SoC 本地检出写在 Git 忽略的 `local.toml` 中，步骤与验证命令见
[docs/LOCAL-DEVELOPMENT.md](docs/LOCAL-DEVELOPMENT.md)。

## 当前进度 / Current status

**2026-11-11**：阶段 5 已全部通过，**阶段 6（存储）正在进行**——「NativeUfs」已匹配
「/arm-io/ufshc」、完成控制器接管并装好自己的请求列表；第一条 SCSI 命令（TEST UNIT READY）
在门铃之后让总线挂起，这是当前唯一的阻塞点。

**2026-11-11**: stage 5 is complete and **stage 6 (storage) is in progress**. `NativeUfs` matches
`/arm-io/ufshc`, takes over the controller and installs its own request list; the first SCSI command
(TEST UNIT READY) stalls the bus after the doorbell. That stall is the only blocker.

| 层 / Layer | 状态 / State |
|---|---|
| 1 启动镜像 | ✅ efisp loader 从 `rawdump` 读 payload 与 FD，校验后把 FD 拷到 `0xFF300000`，`x0` 带 DT 进 SEC |
| 2 UEFI 固件 | ✅ 4 MiB FD 起得来：DXE/BDS、GOP、输入、RAM 盘 + FAT、`AcpiTableDxe` 都在；厂商 UFS 栈保持关闭 |
| 3 UniXNU 启动管理器 | ✅ 菜单、`unixnu.toml` 解析、kit/驱动/ACPI 注入 |
| 4 DarwinBoot | ✅ QEMU 三标记 + 真机交接（`p12/p13 map`、`EBS ok`） |
| 5 内核 | ✅ 入口 → MMU → `pmap_bootstrap` → `arm_vm_init` → `PE_create_console` → 平台专家 → `ArmCpu` → IOKit 匹配 |
| 6 存储 / 根卷 | 🟡 控制器接管完成；首条命令后总线挂起 |
| 7 用户态 | ⬜ 未开始 |

## 阶段 5 的关键修复（存证）

整整一夜的「视频控制台崩溃」是假象：`pexpert/arm64/BCM2837.h` 把 `PLATFORM_PANIC_LOG_PADDR`
硬编码成树莓派地址 `0x3c0fc000`，`check_for_panic_log()` 在建控制台前就映射并读取它。
补丁 `0002` 去掉该宏后，`PE_create_console` → `PE_init_platform(TRUE)` → `machine_startup`
依次通过（每步用 3 秒 PSCI 复位探针确认）。

之后 kext 阶段又依次暴露并修掉：类重复注册（`0006`）、预链接常量段写保护（`0007`）、
未实现 prototype 的 selector、`IOService::waitForService` 的无界等待（`0009` 看门狗，
缺 provider 时直接 panic 报名字）、IOCatalogue 按需加载 AppleImage4（`0011`）、
空构造器 `IOAESAccelerator`（`0012`）、`ArmCpu` 重复注册 CPU 触发的 KPC 断言（`0014`）。

## 存储（阶段 6）现状

### 已完成

- `ShengUfs` 以 `XNU_NATIVE_UFS=1` 链接进内核（`llvm-nm` 可见 `NativeUfs` 符号），
  本地覆盖 `work-local/ShengUfs-canoe` 已按 canoe 的 GCC 偏移修正；
- `NativeUfs` 匹配 `/arm-io/ufshc`（`IOPlatformDevice` + `IONameMatch`），进入 `start()`；
- 控制器寄存器可读：`cap=d5c7031f`、`ver=00000400`、`hcs=0000010f`、`hce=00000001`；
- `probe=3` 成功：停掉固件的请求队列、装上自己的列表（`ba=c3aff000` 读回一致）、
  SMMU 旁路完成。

### SMMU 实测（apps-smmu @0x15000000，stream 0x60）

| 寄存器 | 值 | 说明 |
|---|---|---|
| IDR0 | `0x4c017e7a` | 122 个 stream group，AArch64 stage 1 支持位已置 |
| IDR1 | `0x50000051` | 81 个 context bank、64 页 CB 数组 |
| IDR2 | `0x5211` | stage 1 支持位已置 |
| SCTLR | `0` | **SMMU 全局旁路**，UFS DMA 本来就是物理地址 |

两个坑：IDR1 的 bit29 在块稳定前会读成 1（使 pages 变 128，几何检查拒绝），本地覆盖已屏蔽该位
并重试规划；SCTLR.M=0 时不再编程 context bank（canoe 未实现的 CB 偏移会触发外部中止），
直接把这条 stream 报告为旁路。

### 当前阻塞：门铃写下去总线就锁死

`probe=4`（TEST UNIT READY LUN0）在敲门铃之后没有任何 panic，连「只轮询内存、不读寄存器」的版本
也一样——说明卡的不是读，而是**写门铃本身**（AXI 写永不返回）。2026-11-11 已逐项排除：

| 已验证正确 | 证据 |
|---|---|
| 控制器接管 | `probe=3`：固件队列停掉、请求列表装好、`ba=c3afa000` 读回一致 |
| 时钟 / 电源 | `gdsc=fa22f800->fa22f800`（bit31 已置）、`aggre=08000000`（无 halt） |
| UIC 链路 | `link why=ok mode=11`：DME_HIBER_EXIT 成功、`DME_GET PA_PWRMode = FAST` |
| 请求描述符 | `utrd=00000001 00000000 0000000f 00000000 c3afb000 00000000 00800080 01000000`，首字已按 UFSHCI 改为 bit0 |
| 基址 | `utrlba == phys`，4 KiB 对齐，位于 4 GiB 以下 |

已证伪：DRAM 低位别名（加 `native-ufs-dma-offset=0x8C0000000` 后同样挂起）。

下一步（有依据的两条）：从本机 `boot_a` 提取的厂商内核已重建出 kallsyms
（`ufshcd_probe_hba` / `ufshcd_send_command` / `ufshcd_make_hba_operational` / `__ufshcd_send_uic_cmd`
均可反汇编），对照它的 HBA 初始化与门铃写序列；或在 Android 运行时用 `devmem` 读同一组寄存器作活参考。

## 模型闸门（每次构建必跑）

`scripts/build-and-test.sh` 现在按顺序执行：

1. `./unixnu kernel` → `payload` → `firmware`（都成功才继续）；
2. **官方 smoke 套件**（`package/Scripts/smoke.sh`）六个场景：默认项、键盘选择、无 GOP 降级、
   块切片、kext 暂存、布局；缺 edk2 树时打印 `SKIP` 并写明路径；
3. **QEMU**：真实 payload + `make-dt.py` 生成的 fixture ADT + `no-video=1` + `-d int`，
   每次打印 `MODEL kernel stopped at …`；
4. 三个标记 + 两道新鲜度检查（客户机固件横幅、runner 退出码），任一不满足即失败、不刷机。

## 配置修复记录

| 项 | 变化 |
|---|---|
| `[payload] dram_size` | 从 `[payload.adt]` 移到 `[payload]`；「ADT 内存必须等于硬件 DTS」的守护从此真正执行 |
| `[payload.adt] cpu_regs` | 按实机 DTB `/cpus` 填 8 个 MPIDR |
| `[[userdata.windows]]` | 依据本机 IPSW `BuildManifest.plist` 填 `OS` 与 `SystemOS` |
| `[[payload.block_slice]]` | `payload.py` 已支持生成 `[[block_slice]]`（阶段 6 发布 SHNGROOT/OS/SystemOS 窗口用） |
| Components `os_abi` | `ShengUfs`/`VirtioMmioBlock` 补上 `ios-16.7.16-20H392`（框架树，未提交） |

## 问题清单 / Problems

1. **UFS 首条命令挂起总线**（当前唯一阻塞）。
2. **预链接空洞**：报告里声明了 `IOCryptoAcceleratorFamily` 的 `__TEXT_EXEC` 段，
   但 `kernel.macho` 里 `IOAESAccelerator::IOAESAccelerator` 是 16 个零字节。
3. **可复现性**：`./unixnu status` 报告两个本地源未被 `unixnu.lock` 固定
   （`ShengUfs` 覆盖与 `sm8850` feed）。
4. **内核显示驱动缺失**：`GopFramebuffer` 已选中但没进 `[kernel] archives`，内核侧还没有显示驱动。
5. **输入**：面板是 Synaptics TCM（SPI19 @ `0x1a80000`），目录里的参考实现是 Novatek，
   需要自己写 TCM 驱动。
6. **PMGR 私有 ABI**：`ShengPlatformPower` 的实现锁在 24A446 的 `ApplePMGR` 布局上
   （`static_assert(sizeof(ApplePMGR)==0x4b8a8)`），本机 profile 无法直接使用；
   其构建还依赖 Sheng 参考工程的 candidate 产物。

## TODO（按序）

1. UFS：`probe=5` 唤醒路径 → 若仍挂起，核对链路/DME 与参考时钟。
2. UFS：GPT 读取、`userdata` 窗口、首块校验（`probe=9/10`）。
3. 根卷：生成 userdata 镜像 → 读 OS 卷 UUID → 填 `root_match_volume_uuid` →
   发布 `[[block_slice]]` 窗口 → `NativeUfs` 出根设备。
4. 内核显示驱动（`GopFramebuffer` 进 `[kernel] archives`），随后输入（Synaptics TCM）。
5. 把两个本地源固定进 `unixnu.lock`。
6. 预链接空洞与 PMGR ABI 两项，按后续需要再处理。

## 厂商 UFS 栈为何保持关闭

厂商 `UFSDxe` + `HALIOMMU` 依赖本机不存在的 `DTBExtn`/`NPA`/`HWIO` 提供者，官方路径是
内核侧的 `ShengUfs`（[UFS 移植](https://unixnu.com/zh/porting/ufs)）。
