# GPU 页面要求核对清单（unixnu.com/zh/porting/gpu + adreno740/adreno-metal）

规则：**每次分析前先读本清单并逐条核对状态**；状态只能写「✅ 已做 / ⚠ 部分 / ❌ 未做」，
并附证据（日志行、补丁号、实测数值）。不允许写「应该」。

## §1 匹配 GPU 代际与 OS ABI

| 要求 | 状态 | 证据 |
|---|---|---|
| 确认 GPU 版本，对照寄存器/固件/命令包/内存接口 | ✅ | A840，`qcom,chipid`、`gen80200_*` 固件、HFI ACK |
| 收集寄存器、中断、SMMU context/stream ID、固件、电源域、时钟档位、互连投票、RAM 保留区、扫描输出布局 | ✅ | `tools/gen_z80_gpu_tables.py` 从 `/sys/firmware/fdt` 生成 17 档 OPP、GMU 475/700、ACTLR 0x32b |

## §2 电源与固件

| 要求 | 状态 | 证据 |
|---|---|---|
| GMU 镜像 / SQE / ZAP 三件套 | ✅ | `PANDFW01` 包 + ADT extents |
| OPP 与带宽策略来自板级 | ✅ | 0225 补丁 |
| 提交前确认 GMU 启动与 HFI 响应 | ✅ | `HFI perf table ACKED`、`level 15 done` |

## §3 内存与命令提交

| 要求 | 状态 | 证据 |
|---|---|---|
| ArmSmmu4K context/stream/地址宽度/缓存维护 | ⚠ | 用 0x32b（与 DT 一致），未按 ArmSmmu4K 语义逐条核对 |
| 保留内存与 GPU VA 窗口（扫描输出/固件/ring/client 各自范围） | ✅ | 1 GiB 池、每客户端 4 GiB 窗口 |
| 有界任务确认 CP 初始化/ring 推进/fence | ✅ | vktest `PASS×35`、58 分钟 0 失败 |
| 渲染已知图像并检查结果 | ✅ | `0 of 4096 wrong` |
| 客户端退出/映射生命周期/fence 失败处理 | ⚠ | 有悬挂恢复（2 s），客户端退出路径未验证 |
| **对象上限：每客户端 16384** | ✅（本轮） | 0234：`GPU_MAX_BOS` 4096 → 16384 |
| **BO 池 1 GiB 连续 write-combined** | ✅ | `[bo pool] … free 1023 MiB` |
| **映射自有内存：不做别名检查的批量映射** | ❌ | 未核对 |
| **映射导入内存：缓存类别相同则同调用，否则完整检查** | ❌ | 未核对 |
| **解除映射：≤512 页每页 TLBIVAL，更多按 ASID** | ❌ | 未核对（`[vm unmap]` 日志也因缺计数变量未加） |
| **提交：不做 TLB 失效** | ❌ | 未核对 |
| **启动日志 `[vm map]`/`[vm unmap]`/`[vm kinds]`/`[bo pool]`** | ⚠ | 有 `[bo pool]`、`[vm map]`、`[vm kinds]`；缺 `[vm unmap]` |
| 查询 ABI/能力 → 分配 → 映射 → 填命令 → 提交 → 等 fence | ✅ | 驱动方法 1–16 已实现 |
| CPU 读取前缓存同步 | ⚠ | 提交路径有 flush；CPU 读路径未逐一核对 |
| fence 超时：停队列→确认 DMA 停止→再释放 | ⚠ | 有 reset 恢复，未做「确认 DMA 停止」 |

## §4 构建并加入 Metal

| 要求 | 状态 | 证据 |
|---|---|---|
| `build-turnip-darwin.py --target ios` | ✅ | Turnip `libvulkan_freedreno.dylib` 已装 |
| `build-adreno-metal-plugin.py` | ✅ | 本地 `build_metal_plugin_linux.sh`（`-O2 -DNDEBUG`） |
| **`add-gpu-plugin-data.py --publish` → `--data-ramdisk`** | ⚠ | 我们手工装进系统卷（等效但偏离页面流程） |
| `--clients NAME,...` | ✅ | `NativeAdrenoMetal.clients` = `backboardd` |
| 核对 sandbox / user-client / trust-cache | ⚠ | trust-cache ✅（plugin5 四条）；sandbox 未核对 |
| 「桥接层从不返回 nil encoder，被拒的 pass 得到拒绝的 encoder」 | ✅ | 日志 `commandBuffer refused` 属设计行为 |
| 进程文本行 `[gpu client text]` | ✅ | 已实现并解出 `Compile(NeedsInput(FunctionConstant…))` |
| AIR → SPIR-V → NIR → IR3 | ✅ | metal2vulkan + Turnip |
| **分别测量首次 pipeline 编译与缓存复用时间** | ❌ | 只测了总耗时（45–72 ms/draw），未分开 |

## 待办（按页面顺序）

1. ❌ `[vm unmap]` 日志（需要先加释放计数）
2. ❌ unmap 的 512 页分界
3. ❌ 映射自有/导入内存的批量规则
4. ❌ 首次 pipeline 编译 vs 缓存复用分别计时
5. ⚠ Data seed / `--data-ramdisk` 流程对齐
