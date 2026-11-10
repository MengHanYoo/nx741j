# QEMU 设备模型（先在 QEMU 里跑，再上真机）

用本机收集到的**真实参数**（8 核、256 MiB 交接窗口、真实 payload 的 kernel/ADT/trustcache、
真实 DarwinBoot）在 QEMU 里走完整条启动链的后半段，避免每次改一点就刷机。

## 一条命令

```sh
sudo ./scripts/qemu-nx741j.sh ~/nx741j-build/UniXNU 60      # 第二个参数是超时秒数
```

脚本在 `out/qemu/` 下组一个 ESP，然后启动 `qemu-system-aarch64 -M virt -cpu cortex-a72 -m 16G -smp 8`：

| ESP 内容 | 来源 |
|---|---|
| `EFI/BOOT/BOOTAA64.EFI` | DEBUG 构建的 `UniXNU.efi` |
| `EFI/UniXNU/Kits/DarwinBoot.kit/` | DEBUG 构建的 `DarwinBoot.efi` + Kit.toml（由 kit autoload 加载） |
| `EFI/UniXNU/Payloads/*` | 直接从 `payload.img` 里取出：`kernel.macho`、`.adt`、`.trustcache` |
| `EFI/UniXNU/unixnu.toml` | 生成，xnu 条目 + `qemu-mark=1` |

固件用宿主机的 AAVMF（`/usr/share/AAVMF/AAVMF_CODE.fd`），串口日志就是测试输出。

## 已模型的真实参数

| 参数 | 真机 | QEMU 模型 |
|---|---|---|
| CPU | 8（1+3+4 三簇） | `-smp 8`（同核数，拓扑不同） |
| 内存 | DRAM 5 GiB @0x8C0000000 | `-m 16G`（留足余量，地址不同） |
| 交接窗口 | `owned_memory_size = 256 MiB` | 同（ADT 里的 `darwinboot-memory-size`） |
| payload | `kernel.macho` 49.4 MB + ADT 13 KB + trustcache 1.5 KB | **原样**从真机 payload 取 |
| DarwinBoot | 真机同一份 DEBUG 二进制 | 同 |
| ADT | 真机 ADT（含 `dram-base`/`dram-size`/NVRAM proxy/CPU 节点） | 同 |
| ADT 内容 | fixture 模式用 `make-dt.py --armvirt` 生成；**real 模式用本机 5,332 节点树**（见下） | 两种都支持 |

## 真实 DTB 参数模式（2026-11-11）

```sh
QEMU_ADT=real sudo ./scripts/qemu-nx741j.sh ~/nx741j-build/UniXNU 40
```

`scripts/mk-qemu-adt.py` 用本机 `dts/nx741j-adt.dts`（5,332 节点）生成 ADT 喂给客户机，
**只把 `/memory` 改写到 QEMU 的 RAM**——这是机器模型唯一搬不动的参数，其余地址一律保留真机值：

| 真实 DTB 参数 | 真机值 | QEMU 里的处理 |
|---|---|---|
| DRAM | `0x8C0000000 + 5 GiB`（21 段，最大连续 5 GiB） | **改写**为 `0x40000000 + 16 GiB`（virt 的固定 RAM 窗口） |
| CPUs | 8 个（MPIDR `0/0x100/0x200/0x300/0x400/0x500/0x10000/0x10100`） | ADT 保留真值；机器 `-smp 8`（同核数，拓扑不同） |
| GIC | `interrupt-controller@17000000`（两段 reg、redistributor-stride 0x40000） | ADT 保留；模型只有 virt GIC（`0x08000000`），读真地址会 external abort |
| UFS | `ufshc@1d84000`（`ufs_mem`/`ice`/`mcq_sqd`/`mcq_vs` 四段） | ADT 保留；无模型 |
| SMMU | `apps-smmu@15000000`，stream `0x60` | ADT 保留；无模型 |
| 时钟/复位 | GCC `clock-controller@100000`，9 个 UFS 时钟 | ADT 保留；无模型 |
| 交接窗口 | `owned_memory_size = 0x10000000`（256 MiB） | 保留 |
| 身份 | `/chosen` 的 `dram-base`/`dram-size`、`chip-id`、NVRAM proxy | 生成器按改写后的 DRAM 重算 |

判读：三个标记仍应出现（加载器与 ADT 内容无关）；随后内核会在真机地址上取外部中止，
这就是「模型里没有该设备」的证据：

```sh
grep -A3 'Taking exception' out/qemu/int.log | tail -8
```

2026-11-11 01:41 实测：`BootEntry`/`MARK #29`/`EBS ok` 各 1 次，串口无异常；`int.log` 记录 979 次中止，
最后一次为 `Data Abort, ESR 0x96000010（external abort）, FAR 0xfffffff693b38fe8, ELR 0xfffffff0079a3138`。

与 fixture 模式的差别：fixture 是「纯 QEMU 机器」，用来证明内核本身能跑；real 是「真机地址 + QEMU 内存」，
用来证明 ADT 的每个参数都来自本机、并暴露模型缺哪台设备。两者都不替代真机。

## 判读

日志里应依次出现：

```
UNIXNU: BootEntry 'NX741J (QEMU model)'
MARK #1 … MARK #29        （ADT 解析、窗口分配、15 段拷贝、memory map）
DARWINBOOT: ARM64 va=… pa=… args=… owned=10000000
DARWINBOOT: p13 map, then straight to ExitBootServices
QEMU: EBS ok, entering the kernel      ← PL011 串口见证（只有 qemu-mark=1 才写）
```

最后一行出现即代表：`ExitBootServices` 成功、已跳进内核。之后 QEMU 会静默到超时——
内核在 QEMU 上没有可用控制台，这是预期行为。

## 2026-11-09 首次跑通时抓到的问题

1. **`ExitBootServices` 后仍调用 `gBS`**：无 GOP 的环境下 `DiagDraw` 会去 `LocateProtocol`，
   直接 `Synchronous Exception at 0x0`。真机上因为之前已经缓存了帧缓冲，没暴露。已修（`mBsGone` 守卫）。
2. trust cache 曾经是 IM4P/DER，`ValidTrustCaches` 直接拒绝——QEMU 里同样能复现，
   所以这类格式问题以后不必再上真机试。

## 不能模型的部分（仍然要真机验证）

| 项 | 原因 |
|---|---|
| MSM 外设（UFS、MDSS/DSI、SMMU、TLMM、PCIe） | QEMU virt 没有这些控制器，寄存器访问会失败或读到 0 |
| 真实 UEFI 内存映射 | 真机的 231.7 MiB 碎片问题只在真机存在（已通过平台内存表修复） |
| 固件卷本身（SEC/DXE/MSM 驱动） | QEMU 用 AAVMF 代替我们的 FD |
| 面板/触摸/按键 | 无模型 |

因此：**逻辑与协议问题先在这里跑**，硬件/内存布局问题仍然上真机。
