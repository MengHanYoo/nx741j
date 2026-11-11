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
