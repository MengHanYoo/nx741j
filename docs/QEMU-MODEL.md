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
