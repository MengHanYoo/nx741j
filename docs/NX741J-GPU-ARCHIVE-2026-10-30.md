# NX741J GPU 移植归档（2026-10-30）

努比亚 Z80 Ultra（NX741J / SM8850 / Adreno 840）iOS 16.7 移植中 **GPU 与 Metal** 一层的完整记录。
所有数值来自手机自身的 DT/固件与设备日志，不借用其它机型。

## 1. 最终状态

| 层 | 状态 | 证据 |
|---|---|---|
| 内核 GPU 驱动（GMU/HFI/CP/ring/BO/fence/SMMU） | ✅ **修好并验证** | vktest：`PASS ×35`、`FAIL ×0`、连续 3464 s、像素 `0 of 4096 wrong` |
| GPU 参数来源 | ✅ 手机 DT | OPP 17 档 160–1050 MHz、GMU 475/700 MHz、`qcom,actlr` 全部 0x32b |
| 插件加载 | ⚠ 逐次开机不稳定 | 有的开机 `MARK` 6.8 万条（GPU 合成在跑），有的开机 `0 waited for the GPU`（CPU 合成） |
| 文字渲染 | ✅ 正常 | 优化构建后 `-O2 -DNDEBUG` |
| 壁纸 | ❌ 缺失（黑背景） | `layers mask` 为空 / swap 记录里无 surface id |
| 撕裂 | ❌ 存在 | 直接写正在被 DSI 扫描的面板内存 |
| 帧率 | 13–60 fps（取决于是否走 GPU） | `stats 600 frames in 9999–43443 ms` |

## 2. 今天的四个关键突破

### 2.1 本地插件构建缺陷（影响最大）

`wsl/build_metal_plugin_linux.sh` 原先**没有任何优化标志**（clang 默认 `-O0`）且**未定义 `NDEBUG`**。
改为：

```
clang -std=c++17 -O2 -DNDEBUG -fno-exceptions -fobjc-arc … -bundle … -lc++ -lc++abi
```

效果：**文字从整块马赛克变为正常、壁纸出现过**（该次开机 GPU 路径生效）。
另修正脚本两处：`M2V=$G/m2v-src`、`adapt_ios16.py` 幂等化。

### 2.2 接受 `open type 0x4148`

日志：`PANDORA GPU UC: open type 16712 refused for backboardd`（16712 = 0x4148）。
Turnip 的 Darwin KMD 会打开两次设备；只接受 0x4147 时第二条被拒 → `VkResultCode(-3)`、`no device answer`。
修复后 `NativeAdrenoMetal` 设备创建成功、文字开始渲染。

### 2.3 `CreateQueue` / `CloseQueue`

尺寸表把方法 11/12 标成 `0xffff`（不支持），而 Vulkan 设备创建必须建队列 → `VK_ERROR_INITIALIZATION_FAILED(-3)`。
实现后返回稳定队列 id。

### 2.4 悬挂恢复

日志：`PANDORA GPU HANG: fence 3440 (pending) client 0 thread 3 … first 70970001` ×37。
在 `gpu_watch` 检测到 5 s（后改 **2 s**）无进度时给所有 client 置 `reset`，下次提交带
`CP_RESET_CONTEXT_STATE` + `CP_WAIT_FOR_IDLE` + LRZ/CCU invalidate。
**实测：卡死后能自动恢复**（用户确认）。

## 3. 已证伪的假设（不要再走）

| 尝试 | 结果 |
|---|---|
| `pandora_scanout=1`（DPU 直读） | 更差：图标不绘制、黑块、下拉闪屏 |
| 逻辑屏改 1216×2688 | iPhone X 固件黑屏 |
| `sweetfb_fill=0` | 黑边，马赛克仍在 |
| `pandora_tu_debug=nolrz,nocb` | 无变化 |
| 强制 CP thread 1 | **无法开机**（thread 3 必需，已回退） |
| `clients=*`（删除 publish） | 插件不加载，退回 CPU 合成 60 fps |
| `sweetfb_doublebuffer=1` | 撕裂无改善（撕裂不来自面板半写） |
| `pandora_fbdump` 帧转储 | 转储区始终为空（`PANDFBDM` 未写入） |
| 预建 `m2v` 缓存目录（A1） | 无改善；但日志确认**未导致任何错误** |

## 4. 前端计数器（每 60 s，来自标记流时间戳）

```
t=98 s    d_c=829  d_d=828  d_p=+76   R=+5341   D=+44598    → 72.49 ms/draw
t=158 s   d_c=1323 d_d=1324 d_p=+89   R=+10661  D=+111151   → 45.34 ms/draw
```

判读：**瓶颈是管线创建（AIR→SPIR-V→NIR→IR3 翻译）**，不是像素填充（D 增量翻倍而耗时下降）。
`c…/0` 表示提交 0 次被拒。`p182` 仍在增长 → 变体尚未编译完。

## 5. 120 Hz 的硬性预算

| 环节 | 实测 | 120 Hz 允许 |
|---|---|---|
| 面板拷贝 | 3.5–3.8 ms | 8.33 ms |
| GPU 合成一帧 | 14–36 ms | 4.7 ms |

结论：**只改内核到不了 120 Hz**；内核侧最多把拷贝移出关键路径（预计 57 → 75–80 fps）。

## 6. 关键日志速查

| 日志 | 含义 |
|---|---|
| `VKTEST: PASS (0 failures)` | GPU 渲染与像素正确 |
| `[bo pool] used n/4096 …` | BO 池健康度（曾因 extent 512 耗尽导致 HANG） |
| `client N opened by <进程>` | 谁拿到了 GPU |
| `open type N refused` | 打开被拒（0x4148 缺失的症状） |
| `PANDORA GPU MARK: 0x4147xxxx` | 插件进度/文本（`0x7fff` 分隔行、`0x7000|char` 每字符） |
| `layers mask … drawn …` | 每帧图层集合（判断壁纸层是否提交） |
| `raw swap id … l0..l4` | swap 记录原始字段（判断解析偏移是否正确） |
| `PANDORA GPU RECOVER` | 悬挂恢复触发 |

## 7. 下一步

1. **判定 swap 记录布局**：`raw swap` 的 `l0–l4` 是否全 0 → 决定是解析偏移错误还是 CA 直出。
2. **管线缓存**：确认 `p` 是否开机就接近 180（缓存生效）还是从 0 增长（需开机预热）。
3. **撕裂**：只有 DPU 双缓冲换址能根治；`scanout=1` 目前实现不完整。
