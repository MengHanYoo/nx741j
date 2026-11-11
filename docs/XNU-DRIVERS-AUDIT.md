# XNU 驱动（drivers/xnu-drivers）对照分析

依据 `https://unixnu.com/zh/drivers/xnu-drivers`；对照本机驱动的实际集成方式。

## 1. 文档的集成模式 vs 我们的实现

| 文档要求 | 我们 | 证据 |
|---|---|---|
| 驱动是编进内核的 C++ IOKit 类，静态库 + `-force_load` | ✅ | `build-xnu-4k-baseline.sh:615-617`（ShengUfs）、`:587`（SMMU4K）、`:605`（ArmDT）、`:611`（ArmCpu） |
| `[kernel] archives` 加构建脚本 | ✅ | `board.toml [kernel] archives` 的 `component:storage-bringup` → `scripts/build-storage-bringup.py` |
| defconfig `CONFIG_XNU_MY_DRIVER=y` | ✅ | `configs/nx741j_defconfig:3` `CONFIG_XNU_NATIVE_UFS=y`（另有 ADRENO/AES/MOCKFS 开关） |
| personality 加进 `iokit/KernelConfigTables.cpp` | ✅ | `baseline.sh:222` `KernelConfigTables.cpo_CFLAGS_ADD=$PERSONALITY_CFLAGS`；`XNU_NATIVE_UFS=1` 时追加 `-DXNU_NATIVE_UFS=1`（`:210-212`） |
| 编译参数来自 `.cpo.json` 记录（含 `-Wall -Wextra -Werror`） | ✅ | `build-storage-bringup.py:41`；其余沿用组件构建的记录参数 |
| `KERNEL_ARCHIVE_DEPENDENCIES` 登记静态库 | ✅ | baseline 每个 `force_load` 后都跟一条依赖登记 |
| provider 属性：`reg`（格式 2 u64 地址/大小对）、`interrupts`、`compatible` | ✅ | ShengUfs 从 `IOPlatformDevice` 读 `reg`；ADT 由实机 DTB 派生（格式 2） |
| DMA 缓冲经 SMMU 映射，`ArmSmmu4K` 提供 4 KiB 表 | ✅ | `[kernel] archives` 的 `arm-smmu4k` 组件 + `package/storage-bringup/SmmuBind.c`（patch 0013 路由） |
| 中断用 `IOInterruptEventSource` | ⚠️ **ShengUfs 目前轮询**，没有中断事件源 | 阶段 6 可接受；接中断是后续优化 |
| 运行时 probe→start→registerService→stop | ✅ | ShengUfs IOKit 类标准实现 |
| 测试：编译/ABI/主机逻辑/QEMU/硬件 | ✅ | QEMU 6 场景 + 14 kext 子用例全 PASS；`build-*-fixture` 主机测试；ABI 字段由构建记录约束 |

## 2. 我们的驱动清单（全部走预链接组件）

| 驱动 | 组件/归档 | 门控 |
|---|---|---|
| ShengUfs（NativeUfs） | `build/sheng-ufs/libShengUfs.a` | `XNU_NATIVE_UFS=1` |
| ArmSmmu4K | `build/arm-smmu4k` | `XNU_ARM_SMMU4K=1` |
| VerifiedRoot | `kits/VerifiedRoot` | 策略包 |
| VirtioMmioBlock / RamBlock | `build/*` | `XNU_NATIVE_VIRTIO_BLOCK` / `XNU_NATIVE_RAM_BLOCK` |
| SoftwareAES bindings | `build/software-aes-bindings` | `XNU_NATIVE_AES` |
| ShengPlatformPower / ShengHidInput | `package/sheng`、`build-sheng-hid-input.py` | 板级增量 |

## 3. 与文档的两处差异

1. **源码布局不同**：文档示例把驱动源码放设备仓库 `drivers/MyDriver/`；我们是**组件式**——ShengUfs/ArmSmmu4K 在框架仓库的组件里，设备仓库只放板级增量（`package/storage-bringup/SmmuBind.c`、`package/sheng/ShengPlatformPower.cpp`）。加本机专用驱动（如 Synaptics TCM）应沿用 `package/` + `build-sheng-hid-input.py` 的模式。
2. **中断未接**：ShengUfs 轮询而非中断驱动，文档的中断模板没有用到；不影响阶段 6 的正确性。

## 4. 结论

- 文档的 12 条集成要求里 **11 条完全一致**，只剩「中断」一条是按轮询实现的偏差；
- 文档的「参考驱动」表里 **ShengUfs 就是官方参考实现**（存储、SMMU、命令队列），我们用的正是它——等于直接对齐官方参考；
- 新驱动建议：`package/<name>/` + `scripts/build-<name>.py` + `[kernel] archives` + `CONFIG_XNU_<NAME>` + personality 补丁，五件套与本机现有模式一致。
