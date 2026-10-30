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
| **映射自有内存：不做别名检查的批量映射** | ✅ | 0236 + **METAL53 上机**：自有内存只走一次 `smmu_map_pages`（整段写表 + 一次 `dsb`）。`[vm alias] 3811 maps: 2927 skipped, 884 checked, 0 refused`（t=296.6 s）——自有内存全部落在 skipped |
| **映射导入内存：缓存类别相同则同调用，否则完整检查** | ✅ | 0236 上机：类别 = 写权限。RO/RW 两类并存时 884 次走完整 `gpu_alias_pass`，其余 2927 次直接放行；**refused = 0**，`aliases client` 行 0 条 → 实际没有跨类别共享页；ro/rw 活页 20423/154222 单独统计 |
| **解除映射：≤512 页每页 TLBIVAL，更多按 ASID** | ✅ | 0235 + METAL53 上机：`[vm unmap] 1071 released (365 own 706 imported) 463985 pages, tlb 995 page / 76 asid, 177764 us`（t=296.6 s）——两条分支都在跑，累计 177 ms |
| **提交：不做 TLB 失效** | ✅ | 0235 上机：去掉映射后的 TLBIALL 后，GPU 合成持续 296 s 无 SMMU fault（`0 SMMU faults`）、无 panic、fps 39.7（600 帧/15.1 s），说明「只填无效叶子」成立 |
| **启动日志 `[vm map]`/`[vm unmap]`/`[vm kinds]`/`[bo pool]`** | ✅ | 上机三条齐全：`[vm map] 2740 live (2562 own 178 imported) 682 MiB`、`[vm unmap] … tlb 995 page / 76 asid`、`[vm alias] … 0 refused; ro 20423 rw 154222 pages`（METAL53，t=296.6 s） |
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
| **分别测量首次 pipeline 编译与缓存复用时间** | ⚠ | 插件已按 runtime shader cache 的 miss/hit 把管线创建拆成 `T pipe compile` / `T pipe reuse` 两行（`NativeBridgeMetalFrontend.mm`，补丁 `wsl/metal-patches/0001-*.patch`）；已装进镜像并刷 `METAL54`（cdhash `a6d73531…`），**待读日志** |

## 待办（按页面顺序）

1. ✅ `[vm unmap]` 日志（0235，METAL53 上机确认）
2. ✅ unmap 的 512 页分界（0235，page 995 / asid 76 两条分支都在跑）
3. ✅ 映射自有/导入内存的批量规则（0236，skipped 2927 / checked 884 / refused 0）
4. ⚠ 首次 pipeline 编译 vs 缓存复用分别计时（METAL54 已上机，待日志出现 `T pipe compile` / `T pipe reuse` 两行）
5. ⚠ Data seed / `--data-ramdisk` 流程对齐

## 本轮改动（2026-10-30 23:39，patch 0235）

- `bsd/dev/pandora_gpu.c`：`smmu_unmap` 按 512 页分流（`ArmSmmu4KSyncContextTlbPages` / `ArmSmmu4KInvalidateAllOwnedContext`）；`smmu_map_pages` 去掉映射后的 TLBIALL；新增 `[vm unmap]` 累计行（released own/imported、pages、tlb page/asid、us）。
- `bsd/dev/adreno/ArmSmmu4KProgram.{c,h}`：补入 kit 已有的 `ArmSmmu4KSyncContextTlbPages`（`ArmSmmu4KTlbPageLimit=512`）。
- 编译：`bash /root/local_build.sh 2 /opt/work/out-GPU29 bsd/dev/pandora_gpu.c bsd/dev/adreno/ArmSmmu4KProgram.c bsd/dev/pandora_touch.c iokit/Kernel/arm/PandoraAdrenoGpu.cpp iokit/Kernel/arm/SweetFramebuffer.cpp` → `0 of 5 files failed`；`/opt/work/out-GPU29/kernel.development.pandora` sha256 `c0189454b404513291f3061be29c95713c0df19f2d39556894380a6c8e9c8840`（首版只列 2 个文件，缺 SweetFramebuffer 的 `NX741J KERNEL` 标记，已按 out-GPU28 的 4 文件集 + ArmSmmu4KProgram 重编）。
- 补丁暂存：`/opt/work/pd/0235-nx741j-gpu-unmap-tlb-and-log.patch`（对当前 tree 的增量，`patch -p1 --dry-run` 通过）。
- 装机链：k32 → metal51 payload → loader Image → `METAL51-boot_a.img`（54,362,112 B，sha256 `cb3f9e2b9eb00a249069d521c0e62f38fd83a6fd266f5ca4eb79748ea8e645eb`），四层 marker 均为 1；**未刷机**：23:44 起 adb/PnP 都看不到手机（23:42 还在），等待 240 s 后 `NO_DEVICE`。

## 本轮改动（2026-10-31 00:05，patch 0236）

- `bsd/dev/pandora_gpu.c`：把清单里剩下的两条映射规则落进驱动。
  - 别名类别 `gpu_alias_class(perm, readonly)` = 写权限位（本驱动所有映射固定 normal non-cacheable / outer shareable / unprivileged，类别只剩 RO/RW）；`gpu_map_pages_cls[2]`、`gpu_import_pages_cls[2]` 记每类活页数。
  - **自有内存（BO 池）**：整段一次 `smmu_map_pages`（一次写完全部叶子、一次 `dsb`），不做任何别名遍历——池页在 unmap 完成前不会被任何进程 import。
  - **导入内存**：仅当另一类别有活映射时才走 `gpu_alias_pass`，逐页比对另一类别的映射（pool 连续段或 import 的页表），重叠即 `EEXIST`；覆盖 BO 池的 import 直接拒绝。同类别的重复映射照常放行。
  - 释放时按类别回退页数；`[vm alias]` 每 60 s 打印 maps/skipped/checked/refused 与 ro/rw 活页数。
- 编译：`bash /root/local_build.sh 2 /opt/work/out-GPU31 bsd/dev/pandora_gpu.c bsd/dev/pandora_touch.c bsd/dev/adreno/ArmSmmu4KProgram.c iokit/Kernel/arm/PandoraAdrenoGpu.cpp iokit/Kernel/arm/SweetFramebuffer.cpp` → `0 of 5 files failed`；kernel sha256 `5b2f04bdb080ab86d4a12287c680d51a3578c0eea385c370e1afbaaa8e127ae8`（0235 的是 `c0189454…`，只多 200 字节）。
- 预链接/负载/镜像：k34 `ee322deae84325b593facbb652a2722f11d253bde3f1e9c61e6af3964a0eeef8`；payload `37829c7d485ca0530f2f9fb4fb9ad546c67f3428ab3cdc36e5e777972886d0b1`（54,311,952 B，含 `vm alias`/`vm unmap`/`NX741J KERNEL`）；`METAL53-boot_a.img` 54,362,112 B，sha256 `c55967342e6fa7e0e2430053fdf43ef9f02d05be404699a8eeee90cde355fb2c`。
- 补丁：`xnu/patches/0236-nx741j-gpu-map-alias-rules.patch`（5 个 hunk；对 0236 之前的树 `patch -p1 --dry-run` 通过，实应用后与构建树逐字节一致）。0235 一并收进 `xnu/patches/`。
- **未刷机**：00:00 起 `adb devices` 仍为空、USB PnP 里也没有机器（23:42 之后就掉了），METAL51 与 METAL53 都还没有设备证据。

## 上机验证（2026-10-31 00:20，METAL53 = 0235 + 0236）

日志 `ioslog_m53.txt`（`_log53.py` 从 `ioslog.img` 拉取，环内 65.3–296.9 s、64533 行）。

| 指标 | 实测 |
|---|---|
| GPU 合成 | `PANDORA GPU MARK` 45416 条，全部 `by backboardd`；无 `HANG` / `RECOVER` / `panic` |
| 帧率 | `stats 600 frames in 15113 ms` → **39.7 fps**，`copy avg 3734 us`，600 帧里 389 帧等了 GPU |
| 映射 | `[vm map] 2740 live (2562 own 178 imported) 682 MiB` |
| 别名 | `2927 skipped / 884 checked / 0 refused`，`aliases client` 0 条 |
| unmap | `1071 released (365 own 706 imported) 463985 pages, tlb 995 page / 76 asid, 177764 us` |
| 触控 | `PANDORA TOUCH: 921 reports (921 touch), 0 errors`；`SWEET HID: touch 0 down/up` 坐标连续（如 386,2064 / 491,2425 edge） |
| 前端计数 | `c2528/0 d2528 p628 x1265 R19924 D173676 F7494`：`p` 43→470→628 而 `d` 到 2528，**函数常量修复后管线不再逐帧重建**（先前 `p` 冲到 2492） |

顺带暴露的两条前端报错（下一步线索，非本轮改动引起）：

- `native-metal: no device answer: the device has no answer for supports ConditionalLoadStore`
- `render pipeline: MetalError … call: new_render_pipeline_state, domain: AGXMetalG15X_M1, code: 3, localizedDescription: Rasterizati… problem: UnrasterizedVertexReturn`

## 本轮改动（2026-10-31 00:25，待办 4：管线计时拆分）

- `NativeBridgeMetalFrontend.mm`（插件侧，不在内核树）：新增 `gTimePipelineCompile` / `gTimePipelineReuse`，在三个创建点（render with options、compute with function、compute with descriptor）用 `nmb_device_shader_cache_stats()` 前后的 `misses`/`driver_misses` 增量判断本次是**首次编译**还是**缓存复用**，并累加各自耗时；telemetry 表新增两行 `T pipe compile …` / `T pipe reuse …`（随 `STn` 分块进内核日志）。
- 构建：`bash wsl/build_metal_plugin_linux.sh` → rust 5.1 s、bundle 21,954,296 B，`METAL_LOCAL_DONE`；签名后 sha256 `d06f6058…`。
- 补丁归档：`wsl/metal-patches/0001-pipeline-compile-vs-reuse-timing.patch`（5 个 hunk）。
- 装机（2026-10-31 00:31）：`ldid` + 平台标志 → 新 cdhash `a6d735310d6ce1666614b342ceecbbdb66887d00`；`plugin6.trst` 2765 条（并入旧插件 / Turnip / vktest）；QEMU 写入 `iosroot-SEAL.img`（镜像内 `NativeAdrenoMetal` 22,126,160 B）。
- payload `33d35620…`（54,311,968 B，新 cdhash 在信任缓存里，`a6d73531…` 命中 1 次）；`METAL54-boot_a.img` 54,362,112 B sha256 `4cb9770b…`，已刷 `boot_a` 并重启。
- 回滚留底：`/root/hfswork/old-plugin-2026-10-31/NativeAdrenoMetal`（sha256 `a3521fc6…`），重跑 `02_install_plugin.sh` 换回即可。



