# 步骤 6 · 平台与内存（第 1 阶段）

官方[平台、内存与 OS 服务](https://unixnu.com/zh/porting/platform)要求先描述 CPU/中断资源与内存/DMA，
再按依赖顺序进入存储。设备仓库 revision `83d1cb34b209d3b3c01bb39a027574938fc12bce`。

## CPU 与中断（来自编译后的 DTS）

| 输入 | 取值 | 来源 |
|---|---|---|
| CPU 数量 / MPIDR | 8 个：`0x0,0x100,…,0x500,0x10000,0x10100` | `dts/nx741j.dts` cpus |
| 启动核 | `cpu@0`（`reg = 0x0`） | 同上 |
| PSCI | `compatible = "arm,psci-1.0"`，`method = "smc"` | `sm8850.dtsi` / 编译产物 |
| GIC | `arm,gic-v3`，dist `0x17000000+0x10000`，redist `0x17080000+0x200000`，`redistributor-stride = 0x40000` | 编译产物 |
| 定时器 | `arm,armv8-timer`，`clock-frequency = 0x124F800`（19.2 MHz），4 个 PPI（13/14/11/10） | 编译产物 |
| 簇类型 | SM8850 为同构 Oryon 核心（无 E/P 混合），未加 `unixnu,cluster-type` | 官方仅要求两类核心时填写 |

DTS 编译命令（与官方一致）：

```sh
dtc -I dts -O dtb -i /opt/work/nx741j-build/UniXNU-sm8850-common/dts \
    -o /tmp/hw.dtb dts/nx741j.dts
```

## 内存与 DMA

实机 DTB（`stock/dt/live_fdt.dtb`，SM8850 canoe）的 `memory` 节点共 **21 段**、合计
`0x3CF4F8000`（15.24 GiB，与 `MemTotal 15600240 kB` 一致）。主要区段：

| 区段 | 大小 |
|---|---|
| `0x8C0000000` + `0x140000000` | 5.00 GiB（当前 DTS 选用） |
| `0xA00000000` + `0x200000000` | 8.00 GiB |
| `0x880000000` + `0x2F6FD000` | 0.74 GiB |
| `0x8B0000000` + `0x8500000` | 0.13 GiB |
| `0xDFDA0000` + `0x1FA60000` | 0.49 GiB |
| 其余 16 段（低地址空洞/共享区） | 共 0.87 GiB |

官方 ADT 生成器要求**单一连续 DRAM 范围**（`ADT handoff requires one contiguous DRAM range`），
因此 DTS 必须只保留一段。当前取最大的连续区 `0x8C0000000 + 5 GiB`，`dram_size = 0x140000000`；
完整 21 段布局保留在此，等内核启动、内存映射可用后再按实际交接范围调整。

| 项 | 当前值 | 状态 |
|---|---|---|
| DTS memory 节点 | 单段 `0x8C0000000 + 5 GiB` | 满足 ADT 约束 |
| `[payload.adt] dram_size` | `0x140000000` | 与该段一致 |
| `[payload.adt] owned_memory_size` | `0x10000000` | 加载器自有窗口，待与 Mu SEC 保留区核对 |
| `[payload.adt] psci_method` | `smc` | 与 DTS 一致 |
| `[payload.adt] cpu_regs` | `0,0x100` | 启动核 MPIDR |
| SMMU / DMA 宽度 | `ArmSmmu4K` 未选 | 先完成存储/显示后再按官方读取上下文 |

## 块设备与 OS 服务（官方第 3 节）

| 组件 | 状态 |
|---|---|
| `SoftwareAES` | 已选并参与归档 |
| `DarwinBoot` | 固件内已安装 loader 协议（EL1） |
| `XnuPrelink` | 已选；`runtime_qualified=false` |
| `RamBlock` / `VirtioMmioBlock` / `VerifiedRoot` / `AppleKeyStoreState` | 未选（依赖存储与完整性阶段） |

## 验证

```sh
./unixnu status --json -D ../UniXNU-nx741j
./unixnu build kernel -D ../UniXNU-nx741j   # 链接通过与预链接结果见层 5 记录
```

## 结论与下一步

- 平台/中断描述完整，ADT 输入已显式化；设备侧验证需要内核真正启动（当前层 4 阻塞）；
- **下一步（第 2 阶段）**：UFS 存储。先核对 SM8850 UFS 控制器与 `ShengUfs`（目录标注 SM8550）的寄存器差异，
  再决定是否本地覆盖该组件；
- 内存节点与 `dram_size` 的一致性列为存疑项，内核启动后第一件事核对。
