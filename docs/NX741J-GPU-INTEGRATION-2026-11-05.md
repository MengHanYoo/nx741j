# NX741J GPU 进度整合（2026-11-05）

> 来源：`repos/nx741j/docs/` 下既有 GPU 文档（`GPU_PLAN.md`、`GPU_PORT_MAP.md`、`GPU-PAGE-CHECKLIST.md`、
> `NX741J-GPU-ARCHIVE-2026-10-30.md`、`NX741J-GPU-FIX-2026-10-29.md`、`NX741J-GPU-OPERATIONS.md`），
> 以及 `z80u-collect/gpu-fw/`（gen80200_aqe.fw / gen80200_gmu.bin / gen80200_sqe.fw / gen80200_zap.mbn）。

## 1. GPU 侧已达成（来自 GPU-ARCHIVE-2026-10-30）

| 项 | 状态 | 证据 |
|---|---|---|
| 内核 GPU 驱动（GMU/HFI/CP/ring/BO/fence/SMMU） | ✅ 修复并验证 | vktest `PASS ×35`、`FAIL ×0`、连续 3464 s、像素 `0 of 4096 wrong` |
| GPU 参数来源 | ✅ 手机 DT | OPP 17 档 160–1050 MHz；GMU 475/700 MHz；`qcom,actlr` 全 0x32b |
| 文字渲染 | ✅ 正常 | 插件优化构建 `-O2 -DNDEBUG` 后恢复 |
| 挂死恢复 | ✅ 自动 | `gpu_watch` 2 s 无进度 → reset + `CP_RESET_CONTEXT_STATE`/`CP_WAIT_FOR_IDLE` |
| 壁纸 | ❌ 黑背景 | `layers mask` 为空 / swap 记录无 surface id |
| 撕裂 | ❌ 存在 | 直接写正在被 DSI 扫描的面板内存 |
| 帧率 | 13–30 fps | `stats 600 frames in 9999–13443 ms` |

### 关键代码修复（已在插件侧）

1. `wsl/build_metal_plugin_linux.sh` 增加 `-O2 -DNDEBUG`（原来无优化，文字整块马赛克）；
2. 接受 `open type 0x4148`（Turnip/Darwin KMD 二次打开设备）；
3. 实现 `CreateQueue`/`CloseQueue`（尺寸表原标 0xffff → 设备创建失败 -3）；
4. 挂死恢复逻辑（2 s 超时 + 上下文重置）。

### 已证伪的假设（不要再试）

`pandora_scanout=1`、逻辑分辨率改 1216×2688、`sweetfb_fill=0`、`pandora_tu_debug=nolrz,nocb`、
强制 CP thread 1、`clients=*`、`sweetfb_doublebuffer=1`、`pandora_fbdump` 帧转储、预建 m2v 缓存目录。

## 2. 待上机的构建（2026-10-31 00:05）

- `METAL53-boot_a.img`（24,362,112 B，sha256 `c5596734…`），含 0235 + 0236；
- 上机检查顺序：`[vm alias]`（skipped 占绝大多数、refused 应为 0）→ swap 记录 `l0–l4` 是否全 0 → 管线创建耗时。

## 3. 下一步 TODO（继承）

1. **判定 swap 记录布局**：`raw swap` 的 `l0–l4` 是否全 0 → 决定是解析偏移错误还是 CA 直出；
2. **管线缓存**：确认 `p` 是否开机即接近 180（缓存生效）还是从 0 增长（需开机预热）；
3. **撕裂**：只有 DPU 双缓冲换址能根治；`scanout=1` 目前实现不完整；
4. 120 Hz 硬性预算：面板拷贝 3.5–4.8 ms、GPU 合成一帧 14–16 ms，**只改内核到不了 120 Hz**。

## 4. 与当前六层链的关系

- GPU 属于第 5/6 层的显示/图形能力，依赖第 2 层 UEFI 放行后、内核启动并加载 GPU 驱动；
- 当前第 2 层卡在 SMEM（见 `NX741J-HANDOFF-2026-11-05.md`），GPU 的待办（壁纸/撕裂/帧率）
  属于第 6 层运行期问题，不阻塞第 1/2 层；
- `gpu-fw/` 下的 gen80200 固件是 Adreno 840 的 AQE/SQE/GMU/ZAP，供内核 GPU 驱动加载（第 5 层）。

## 5. 整合说明

- 本文把散落在多个 GPU 文档中的**状态、已证伪假设、待办**收敛到一页，作为当前工作区的 GPU 索引；
- 原始细节仍以 `GPU_PLAN.md`（47 KB）/`GPU_PORT_MAP.md`（38 KB）/`GPU-PAGE-CHECKLIST.md`（12.5 KB）为准；
- 本文件不改动 GPU 代码与构建，仅做进度整合。