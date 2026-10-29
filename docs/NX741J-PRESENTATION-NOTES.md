# NX741J 呈现层排查记录（2026-10-29 晚）

GPU 内核驱动已修好并验证（vktest：PASS×35、FAIL×0、58 分钟、像素 0 错）。
本文记录**剩下**的呈现问题：壁纸纯色、Settings 文字马赛克。

## 已排除

| 假设 | 结论 | 依据 |
|---|---|---|
| GPU 渲染错误 | 排除 | vktest 像素回读 `0 of 4096 wrong`，三角形 `0 clearly wrong` |
| BO 泄漏 / 池耗尽 | 排除 | `[bo pool]` 长期稳定（1291/4096，free 548 MiB） |
| ring 覆盖 | 已修 | `ring_wait_space()`（0226），修复后 58 分钟零失败 |
| 客户端白名单 | 排除 | `NativeAdrenoMetal.clients` 改成 `*`（所有进程）后无变化 |
| bundle 封装 / 信任 | 已修 | 封装 + 插件/Turnip/vktest 三个 cdhash 进 static 模块 |
| 深色帧缓冲 / 压缩 | 已关 | `com.apple.backboardd.plist` 三个 CAEnable* = false（已确认在镜像里） |
| swap 语义 | 正常 | `swap wait … -> 0xe00002d5` 是 `kIOReturnBusy`（不等待轮询），非故障 |
| 逻辑屏分辨率 | 回退 | 改 1216×2688 会让 iPhone X 固件黑屏 |

## 已确认并修复的一项

**CoreAnimation 会轮换 surface**（日志 `layer 0 surface 2 -> 3 -> 9 …`）。
之前只按请求里的脏矩形拷贝，surface 一换其余区域就是旧帧 → 残影/马赛克。
修复：检测到 surface 变化就**整帧拷贝**（`fLastSurf[layer]`）。
日志已能看到 `SWEET FB: layer 0 surface 2 -> 3, full frame 1125x2436`，
但**马赛克仍在**，说明这不是唯一原因。

## 实测性能事实

```
SWEET FB: stats 600 frames in 180473 ms, copy avg 3210 us, 600 swaps, 126 waited for the GPU
```
即 180 秒 600 帧 = **3.3 fps**；拷贝只占 3.2 ms，瓶颈在每帧的 GPU 工作
（与文档「冷启动 77 个 pipeline 约 43 秒」一致）。跑满 10 分钟后速率与画质均未改善。

## 结论：问题在 Metal 前端（AdrenoMetal/metal2vulkan/Turnip），不在内核驱动

按 UniXNU 的分层：

- `Adreno740`（内核 GPU：GMU/CP/BO/fence/SMMU）→ **已达标**
- `AdrenoMetal` + `metal2vulkan` + Turnip（AIR→SPIR-V→NIR→IR3 翻译、CA uber shader）→ **残余问题在这里**

## 下一步建议（按成本排序）

1. **用 CI 的插件产物替换本地构建**：`metal.yml` 在 macOS 上用 Xcode + `codesign` 构建，
   而我们的插件是 Linux 上用手搓工具链（clang-ios + ld64.lld + ldid）编的。
   文档提到「NDEBUG release 构建」会关掉 Mesa 的 per-pass assertion 与 NIR 校验，
   本地构建未显式定义 `NDEBUG`，可能改变翻译行为。先换成 CI 产物对比。
2. 若 CI 产物同样马赛克 → 是 metal2vulkan 对 CA uber shader 的翻译缺陷，
   需要打开 metal2vulkan 的 debug 端口（`pandora_tu_debug=` 已在内核命令行解析）并收集被拒的 pass。
3. 呈现侧仍可继续：把 `sweetfb` 的拷贝改成「等 fence → 整帧 invalidate → 整帧拷贝」，
   并逐帧比较 surface 与 panel 的校验和，确认拷贝无误。
