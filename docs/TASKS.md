# 任务总表（当前工作区：`E:\default-workspace\z80u-collect`）

> 把此前四个工作区里的任务、产物线索与未完成项，统一收敛到这里。
> 更新：2026-11-11 14:35（Asia/Shanghai）

## 1. 工作区清单与归属

| # | 工作区 | 内容 | 处理 |
|---|---|---|---|
| 1 | `C:\Users\MengHanYooo\Documents\deepseek-harness\default-workspace` | 旧 UniXNU 克隆（`UniXNU/`）、git 辅助脚本（`_tools/gh.ps1`、`gitcache.ps1`）、`port_trace.txt` | 只保留 `port_trace.txt` 作证据，已复制到 `_tmp/consolidated/`；其余不再是工作副本 |
| 2 | `E:\default-workspace` | iOS 固件 `ipsw/`（16.7.16 20H392 与三个 dmg）、`tools/`（platform-tools、ffmpeg）、`FMMT_Build.log`、`x.bin`、`分配` | 固件与工具就地保留（体积大、只读）；日志复制到 `_tmp/consolidated/` |
| 3 | WSL：`/opt/work`、`/home/loongko` | UEFI 树 `/opt/work/uefi-sheng-main`、构建树 `/home/loongko/nx741j-build/UniXNU`、本地组件覆盖 `/opt/work/work-local/ShengUfs-canoe` | 构建必须就地运行；**唯一权威源码副本**是 `work-local` 与设备仓库，已在本文档登记 |
| 4 | `E:\default-workspace\z80u-collect` | **当前工作区**：`repos/UniXNU-nx741j`（设备仓库）、`repos/UniXNU`（框架）、`stock/`、`wsl/`（刷机脚本）、`_tmp/`（镜像与证据） | 全部新产物、脚本、证据只写这里 |

## 2. 阶段状态

| 阶段 | 状态 | 依据 |
|---|---|---|
| 1 启动镜像（efisp → payload + FD） | ✅ | 三段刷入并校验；`readback match` |
| 2 UEFI 固件 | ✅ | DXE/BDS、GOP、输入、RAM 盘 + FAT、ACPI 注入 |
| 3 UniXNU | ✅ | 菜单、`unixnu.toml`、kit/驱动/ACPI |
| 4 DarwinBoot | ✅ | QEMU 三标记 + 真机 `p12/p13 map`、`EBS ok` |
| 5 内核 | ✅ | 入口 → MMU → pmap → console → 平台专家 → `ArmCpu` → IOKit 匹配 |
| 6 存储/根卷 | 🟡 | 控制器接管 ✅；**首条 SCSI 命令仍锁死总线** |
| 7 用户态 | ⬜ | — |

## 3. 阶段 6 的全部已知事实（2026-11-11 实测）

| 项 | 读数 / 结论 |
|---|---|
| 控制器 | `cap=d5c7031f`（64 位寻址）、首次 `hcs=0000010f`（三队列就绪） |
| GCC（canoe 偏移） | GDSCR `0x77004`、AXI `0x77018`、**AHB `0x77028`**、UniPro `0x7706c`；`gdsc=fa22f800`（bit31 已置）、`ahb=08000001`（已使能、门控已关） |
| 链路 | UIC 可用，`DME_HIBER_EXIT` 成功，`PA_PWRMode=FAST`；link bring-up 前后 `hcs` 均为 `0x10F` |
| MCQ | Android 活参考 `use_mcq_mode=Y`、`mcq_sqd@0x1da5000`、8 条 `qcom-mcq-esi` 在计数；但本机启动链下 `mcqcfg=0`（未启用），关 MCQ 是空操作 |
| 关键现象 | 写入 `UTRLBA + UTRLRSR=1` 后 `hcs` 由 `0x10F` 掉到 `0x08`（UTRLRDY/UTMRLRDY 未置位）；敲门铃 `0x58` 即总线锁死（连 panic 画屏都被堵） |
| 已排除 | 描述符三版首字（`0x10000000` / bit0 / 厂商 bit28\|bit24）、DMA 两种取址（`pa` 与 `pa+0x8C0000000`，`ptoa==pa`）、自分配缓冲与固件列表、HCE 重启、`0x4C`、UTMRL、AHB 门控、低/高窗口 |

## 4. 待办（按序）

1. **UTMRL 就绪**：本版已给 UTMRL 独立页（`dmaPa+0x1000`）与 `0x80=1`，UCD 移到 `+0x2000`；判据是 `hcs & 0x0E == 0x0E`。
2. **若 `hcs` 仍为 `0x08`**：说明控制器拒绝我们给的队列基址 → 改走 **MCQ 门铃**（`mcq_sqd+0x8`，值 `(slot+1)<<5`），并按 `mcq`/`mcq_vs` 两段配置队列。
3. **GPT 读取**（`probe=9`）：主 GPT 头在 LBA1，校验 `EFI PART` 与 CRC32。
4. **根卷**：`userdata` 镜像 → OS 卷 UUID → `root_match_volume_uuid` → `[[block_slice]]` → `NativeUfs` 出根设备。
5. 内核显示驱动（`GopFramebuffer` 进 `[kernel] archives`），随后 Synaptics TCM 输入驱动。
6. 把两个本地源（`ShengUfs` 覆盖、`sm8850` feed）固定进 `unixnu.lock`。
7. 预链接空洞（`IOAESAccelerator` 16 字节零）与 PMGR 私有 ABI 两项，按需处理。

## 5. 产物索引（本工作区）

| 路径 | 内容 |
|---|---|
| `repos/UniXNU-nx741j/` | 设备仓库（README、docs、patches、board.toml、scripts） |
| `repos/UniXNU-nx741j/docs/STATUS-2026-11-11.md` | 阶段 5/6 完整分析与问题清单 |
| `repos/UniXNU-nx741j/docs/UFS-VENDOR-DIFF.md` | 厂商内核反汇编对照 + Android 活参考 |
| `repos/UniXNU-nx741j/docs/STEP6-UFS.md` | canoe GCC 偏移表 |
| `_tmp/payload-*.img` | 各实验 payload（`utmr2` 为当前） |
| `_tmp/fd-*.bin` | 固件 FD 变体（当前用 `fd-rev.bin`） |
| `_tmp/vendor/` | 厂商内核 `vendor-kernel.bin/.elf`（反汇编用） |
| `_tmp/consolidated/` | 从旧工作区收敛来的日志证据 |

## 6. 闸门（每次构建必跑）

```
UNIXNU_ADT_NO_INTERRUPTS=1 XNU_NATIVE_UFS=1 \
  repos/UniXNU-nx741j/scripts/build-and-test.sh /home/loongko/nx741j-build/UniXNU
```

kernel → payload → firmware → 官方 smoke（6 场景，`KEXT_TIMEOUT=180`，失败重试一次）→ QEMU（fixture ADT，三标记）→ 新鲜度检查。**未过闸门不刷机。**

## 2026-11-11 晚：MCQ 路径的实验结论（重要）

今天把 UFS 从「传统门铃」一路推到「MCQ」，每一步都有屏幕读数支撑，结论如下：

| 实验 | 读数 | 结论 |
|---|---|---|
| 传统门铃 0x58（自分配缓冲） | 写门铃后总线锁死，panic 画屏也被堵 | 传统门铃不可用 |
| 传统门铃（固件列表页 0xc3afc000） | 同样锁死 | 不是缓冲位置问题 |
| MCQ（按厂商配方，队列在自分配内存） | 同样锁死 | MCQ 门铃写下去也挂 |
| MCQ（队列放固件页 0xc3afc000） | `far=sq+3`、`esr=0x96000047`（translation fault，写） | **内核无法通过 IO 映射写那段 DRAM**，该实验做不了 |
| 链路状态 | `hcs 0000010F -> 0000010F`（接管前）、`PA_PWRMode=FAST` | 链路本身正常 |
| GCC（canoe 偏移） | `gdsc=fa22f800`、`ahb=08000001` | 时钟/电源正常 |
| MCQ 配置（Android 活参考） | `use_mcq_mode=Y`、`mcq_sqd@0x1da5000`、8 条 `qcom-mcq-esi` 计数在涨 | 厂商确实跑 MCQ |

### 冒烟结果汇总

- `probe=6` 实际被 `dumpFirmwareList` 占用（旧的固件列表转储），MCQ 代码从未执行；已改用 `probe=14`。
- 传统路径写 `UTRLBA/UTMRLBA + run-stop` 后 `hcs` 从 `0x10F` 掉到 `0x08`（`UTRLRDY/UTMRLRDY` 清零）。
- 无论门铃写哪条路径，都表现为「写下去不返回、总线锁死」。

### 尚未排除的根因（按可能性）

1. **UFS master 的 DMA 被 SMMU/防火墙拦住**：我们读到的 `SCTLR=0`（全局旁路）可能只对 apps_smmu 有效，UFS 可能走另一路或需要显式建立映射。
2. **控制器仍处于固件留下的 MCQ 配置**，其 MCA 指向固件内存；我们改指自己的队列时，硬件未接受（未回读校验）。
3. **SM8850 的 UFS 电源域**（`mcq_vs` 段 / 额外时钟）未就绪，硬件在门铃后进入错误状态。

### 下一步（需要你选）

| 方案 | 内容 | 代价 | 信息量 |
|---|---|---|---|
| **A（推荐）** | 在 Android 侧写一个极小的内核模块，在真实 I/O 进行中把 MCA/SQTP/CQ/`mcq_vs` 与 SMMU 相关寄存器导出（`adb root` 已有 KSU），得到**工作中**的寄存器快照 | 需要匹配 vermagic/KMI 编译一次模块 | 最高：直接给出可用配置 |
| B | 继续在固件侧排列组合（队列位置、MCA 回读校验、ESI） | 每次 ~30 分钟 | 低 |
| C | 架构改道：由引导器提供块设备服务给内核 | 数天 | 绕开驱动但不解决根因 |

> 今天的代码与配方已分别落在 `docs/UFS-MCQ-RECIPE.md`、`docs/UFS-VENDOR-DIFF.md`、本文件与本地组件覆盖里。

## 2026-11-11 晚续：T1 与 S1 的结果

| 实验 | 内容 | 结果 |
|---|---|---|
| T1 | 队列条目**全零**，只写 `SQTP` | **仍然锁死**（无字） → 写门铃/取指通路本身不可用，与描述符、UCD、缓冲内容无关 |
| S1 | 敲铃前把 apps_smmu 的 stream `0x60` 强制写成 S2CR TYPE=bypass | 仍然锁死 |
| SQ 启动 | 敲铃前写 `SQRTC=0`（start）并等 `SQRTS` | 仍然锁死 |

结论：门铃写下去不返回这一现象，与模式（传统/MCQ）、队列位置、SMMU 旁路、队列启动都无关。
剩余候选：该 master 的 DMA 被固件级 XPU/防火墙拦在可用窗口之外，或控制器处于半初始化状态。

### 待做（下次继续）

1. **MCA 回读验收**（payload 已构建：`_tmp/payload-mcaread.img`，diagnostics=5）：只在敲铃前 panic，打印 MCA 各字段回读值、`hcs/hce/mcqcfg/cap/mcqcap`——判断我们的 MCA 写入是否真被硬件接受。
2. 若 MCA 回读一致 → 转 T2（Android 侧内核模块取工作中配置）。
3. 若 MCA 回读不一致 → 修 MCA 写入顺序（先 `0x300` bit0、再 MCA、最后 ATTR）即可复测。

### 设备恢复记录

- 官方镜像备份：`E:\Nubia_Flash_Brick\backup\ZTEToolBoxParBak_2026.11.09_14.05.38.363\images`（含 `efisp.img`、`misc.img`、`boot_a.img`、`init_boot_a.img`）
- 分区映射（从 `for_ROM_maker/orig/gpt_main*.bin` 解析）：`misc` LUN0 LBA 8200（128 KiB）、`boot_a` LUN4 LBA 119078、`init_boot_a` LUN4 LBA 360742、`efisp` LUN4 LBA 748894
- 本次故障原因：反复失败启动使 `misc` 里 `slot-unbootable:a=yes`；写回官方 `misc` 后由用户自行恢复 Android
- 官方 `efisp.img` 的 SHA256 = `4df18359…`，与仓库内 `efisp-stock-rollback.img` 一致

## 2026-11-11 深夜：预链接空洞的结论 + MCQ v2

### 预链接空洞（按官方 prelink 文档核对）

| 项 | 结果 |
|---|---|
| 空洞位置 | `__PLK_TEXT_EXEC` 内共 13 个空洞；最大三个 3.4 MB / 6.5 MB / 646 KB |
| 崩溃地址归属 | `0xfffffff004a80038` 在原始 kernelcache 里属于 `com.apple.driver.AppleJPEGDriver.__TEXT_EXEC`（0x5a68000..0x5a87a54） |
| 填充验证 | 把该组件加入 roots 后：组件 12→14、extent 覆盖该地址、字节为真实指令、闸门全过 |
| 全填代价 | 13 个空洞共需 **126 个组件**，`kernel.macho` 增大约 10.7 MB |
| 决策 | **保留现状**（只加 AppleJPEGDriver），不追求填满空洞 |
| 遗留问题 | `__LINKINFO` 里 `IOAESAccelerator` 等符号仍是 `A` 型绝对地址、未按 slide 修正 → 记为独立问题 |

### MCQ v2（照抄厂商实测配置）

用只读内核模块从运行中的 Android 取到的真实值（`_tmp/t2/mcq_live_dump.txt`）：

```
CAP=d5c7033f MCQCAP=20e0071f HCS=0000010f HCE=00000003
MCQCFG=00000003 MAC=00003f00
mca[i] sqattr=800X0207 sqlba=0x71000+0x2000*i cqattr=80000207 cqlba=0x72000+0x2000*i
       sqdao=0x21000+0x100*i cqdao=0x21080+0x100*i
UTRLBA=0x38000 UTMRLBA=0x39000
SMMU gr0=00ee0406（bypass）
```

对应改动（`NativeUfs::ufsMcqTurV2`，probe=14）：

1. `HCE=3`、`MCQCFG=3`、`MAC=0x3f00`（N=64）、`qsize=0x207`、`nutrs=64`；
2. 配 9 个队列（0 号带 CQ id），`SQATTR/CQATTR` 按 `0x80000000|qsize`；
3. **队列放在低物理地址** `SQ=0x71000 / CQ=0x72000 / UCD=0x73000`，用 `IOMemoryDescriptor::withPhysicalAddress` 映射（对齐厂商实测地址段）。

闸门：kernel → payload → smoke(6) → QEMU 全过；待刷入 payload `_tmp/payload-mcqv2.img`（`7dcec075…`）。

## 2026-11-12 凌晨：六份官方文档对照审计（子智能体）

| 文档 | 提交 | 结论 |
|---|---|---|
| requirements / configuration / building / log-capture / security | `31ddb90` | 主机走 Linux 路线；输入 8/11；`fastboot boot` 前提不满足（用 efisp 替代）；日志卡在 QR；安全绕过 Image4/AMFI |
| concepts/payload | `8442769` | 布局 4/5（无 Data 种子）；卷容量 81%；交付通道 rawdump → FDT initrd |
| concepts/boot-chain | `03ae27e` | 交接 2/4 逐条一致；三处刻意偏差；阶段 6 卡存储 |
| reference/boot-configuration | `05b7d8f` | 用 4/12 表全部合规；只差 `[[block_slice]]` |
| drivers/kexts | `450e2df` | 发布版 DarwinBoot 拒绝暂存 kext → 预链接内置是正确路线 |
| drivers/xnu-drivers | `86b4452` | 12 条要求 11 条一致；唯一偏差：ShengUfs 用轮询、无 IOInterruptEventSource |

### 优先级（按对当前阶段是否阻塞）

1. **阻塞**：阶段 6 的 UFS 接管；下一步是 pandora 同款序列（清 `MCQCFG` bit0、不写 UTMRL、`dw0` 去 bit0）——payload `449bd302…` 已刷入待判读。
2. **高危**：`unique_chip_id` 仍是占位 `00000000098EE0C3`；需在 Android 上 `getprop ro.boot.unique_chip_id` 取真值。
3. **阶段 7 前**：Data 种子 → `userdata`/`SHNGROOT` 镜像；`[[block_slice]]` 映射已就绪。
4. **结构性约束**：固件 FV 只剩 **112 B**、FD 已满 4 MiB；任何新模块（厂商 UFS 栈、新驱动、额外 ACPI）都必须先删减组件。
5. **bring-up 临时策略**：0003/0004/0011 绕过 Image4/AMFI；日志导出器受限于 Mu 的 `LocateHandleBuffer(AllHandles)` 返回 0；正式启用前需恢复官方路径。

## 2026-11-12 00:0x：pandora 同款序列的结论（重要）

payload `449bd302…`（同 SoC 上实机跑进 iOS 的序列：清 MCQCFG bit0、不写 UTMRL、不碰 SMMU、
按位门控 `HCE&1`/`HCS_DP`/`HCS_UTRLRDY`/`UTRLDBR==0`、`UTRLBA` 回读校验、`dw0=bit28|bit24`、门铃 `0x58`）
在本机同样表现为**写门铃后总线锁死**（只有彩带，无 panic）。

### 由此可以判定

| 类别 | 状态 |
|---|---|
| 描述符编码（三种写法） | 排除 |
| 门铃路径（传统 0x58 / MCQ SQTP） | 排除 |
| 队列数量与深度（1/9 队列、nutrs 32/64） | 排除 |
| SMMU（旁路 / 不碰） | 排除 |
| 时钟与链路（gdsc/ahb/hcs=0x10F/PA_FAST） | 正常 |
| **驱动写法** | **排除**（同 SoC 成功序列也挂） |

### 剩余假设（都需要固件/平台层信息）

1. **互连（ICC/RPMh）带宽投票缺失**：厂商内核通过 ICC 为 UFS↔DDR 路径投票；我们的内核没有 RPMh 驱动，
   若该路径在 ABL 之后被回收，控制器 DMA 会永远等不到响应——与“写门铃不返回”完全吻合。
2. **XPU/防火墙**：UFS master 只被允许访问固件为它划定的 DDR 窗口，我们的分配不在其中。
3. 控制器处于我们未复现的初始化状态（ABL 与我们启动链的差异）。

### 下一步（需新信息，不再盲刷）

- 用 Android 侧内核模块（T2 路径已打通：ACK 6.12.23 树 + 设备 config + 3372 条 CRC，`this_module=0x640`、
  vermagic 完全一致）读 **ICC/RPMh 相关寄存器**与 `apps_smmu` 的 S2CR，确认 UFS 路径是否被投票/放行；
- 或在我们的内核里补一个最小的 RPMh/ICC 投票（把 `ufs_phy_axi`/`ufs_phy_ahb` 的带宽请求拉满）再试门铃。

## 2026-11-12 00:2x：休眠假设排除，结论收敛到「DMA 可达性」

| 实验 | 结果 |
|---|---|
| pandora 同款序列（`449bd302…`） | 彩带，无 panic |
| pandora 序列 + 敲门铃前 `DME_HIBER_EXIT`（`c509b026…`） | 彩带 → **休眠时序不是原因** |
| Android 侧 ICC 实测 | 空闲票数 0；I/O 时 819200 / 5836800（厂商会投票，但不投票也能读寄存器） |

### 判定

所有软件侧变量（描述符编码、门铃路径、队列数量/深度、SMMU、时钟链路、休眠时序、驱动写法）均已排除。
剩余唯一解释：**UFS 控制器无法对我们内核拥有的内存做 DMA**——取指不返回，导致写门铃永不完成、互连锁死。

pandora 的内核窗口在 `0x80000000` 段（`mkadt` 的 PHYS_BASE），我们的窗口在 DarwinBoot 分配的 `0xb4xxxxxx`，
两者对 UFS master 的可达性不同。

### 下一步（二选一，均需固件层改动）

| 选项 | 做法 | 风险 |
|---|---|---|
| **A（推荐）** | 把 DarwinBoot 内核窗口搬到 `0x80000000` 段（固件内存表 + `AllocateWindow` 候选地址 + ADT memory 描述配套） | 之前单改 `0x8C0000000` 导致内核不启动，这次必须同时改 ADT 描述 |
| **B** | 固件在 EBS 前把 UTRD/UCD 预填到它自己的 UFS 队列页，XNU 只写门铃 | 完成状态在 XNU 映射不到的页上，需与 A 结合 |

## 2026-11-12 00:0x：低段内核窗口不可行（硬约束）

实测固件直接拒绝：

```
MMU: 46 regions
"Kernel_Window" (0x83000000) and "PIL_Reserved" (0x8AA00000) Overlap!
```

| 约束 | 数值 |
|---|---|
| 低段连续空闲（PIL 之下） | `0x82600000..0x8AA00000` ≈ **132 MB** |
| payload 工具允许的最小内核窗口 | **256 MiB**（`Owned memory must be 256 MiB to 8 GiB in 2 MiB steps`） |
| PIL 保留区 | `0x8AA00000..0xA7D00000`（466 MB，固件保留，不可动） |

→ **把内核窗口搬进低段在数学上不可行**，除非改工具的最小窗口限制。

### 唯一可走的路（待评估）

在框架仓库 `scripts/unixnu/payload.py` 把最小窗口从 256 MiB 降到 128 MiB，
窗口放 `0x82600000..0x8A600000`，让内核所有分配落在 UFS 可达的低段。
代价：内核可用内存从 256 MB 降到 128 MB（需验证 XNU 能在此尺寸下完成阶段 6）。

### 本轮其它结论

- 内核 IO 窗口只映射 MMIO，不映射窗口外 DRAM：`0x71000`、`0xc3afc000`、`0x83000000` 均写入 fault；
- 队列必须落在内核**已管理**的内存里，而该窗口地址受固件布局限制。
