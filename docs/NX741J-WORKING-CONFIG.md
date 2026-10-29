# NX741J 已验证可用的 GPU 配置（2026-10-29 14:30）

本文件记录**实测通过**的配置。GPU 侧已用 vktest 验证：
**PASS ×35、FAIL ×0、连续 3464 秒（58 分钟）、像素回读 0 错**。

## 1. 内核补丁（按编号顺序应用）

| 补丁 | 内容 |
|---|---|
| `0224-pandora-touch-z80-rails.patch` | 触摸供电改成 Z80 的 `L4B_E0` / `L14B_E0` |
| `0225-nx741j-gpu-opp-from-dt.patch` | GPU OPP 17 档（160–1050 MHz）、GMU 475/700、bus 投票，全部来自手机 DT |
| `0226-nx741j-gpu-ring-space-and-pool.patch` | **GPU 稳定性的关键**：`ring_wait_space()` 防止写指针覆盖未消费命令；`GPU_POOL_EXTENTS 512→1024`；`[bo pool]` 与 `ALLOC FAIL` 诊断 |
| `0227-nx741j-gpu-progress-marker-log.patch` | 打印插件的 `PANDORA GPU MARK: 0x4147xxxx`（解码成 ASCII 即为插件进度/错误） |

## 2. 启动参数（实测）

```
debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress
rd=ufsroot0 ufsrw=1 rootrw=1
unentitled_ios_sim_launch=1 ignition_level=1 launchd_unsecure_cache=1
wdt=-1 wdt_test=no_user_checkin
sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=0
pandora_gpu=15 pandora_gpu_metal=1
pandora_touch_gpio117=0
sweetsample=lsd,SpringBoard
```

说明：

- `pandora_gpu=15`：最高级（含用户客户端）。14 级不够。
- `pandora_gpu_metal=1`：发布 `IOAcceleratorES` 服务，Metal 才会找插件。
- `sweetfb_fill=1`：把 1125×2436 最近邻放大到面板（会有马赛克，见第 5 节）。
- `pandora_scanout=0`：CPU 拷屏；`=1` 曾试过，画面无改善（见第 5 节）。

### 跑 vktest 时额外加

```
sweetexecswap=bluetoothd:/usr/bin/vktest
```

且 **`com.apple.bluetoothd` 必须在 disabled.plist 里是 `false`**（否则宿主进程不启动，
替换不会发生）。

## 3. 系统镜像里必须有的文件

```
/System/Library/Extensions/NativeAdrenoMetal.bundle/
    NativeAdrenoMetal            平台标志 + ad-hoc 签名，cdhash cbce9ba2…
    Info.plist
    _CodeSignature/CodeResources （ldid 封装）
    libvulkan_freedreno.dylib    Turnip，cdhash 899d39cd…
/System/Library/Extensions/NativeAdrenoMetal.clients   → backboardd / vktest
/System/Library/Extensions/NativeAdrenoMetal.publish   （publish 模式，可选）
/usr/bin/vktest                                        cdhash 5bdf5dbd…
```

信任缓存（static 模块，共 2763 条）必须同时包含：插件、Turnip、vktest 三个 cdhash。
**只信任插件、不信任 Turnip，会报 `LoaderNotFound`**（很具误导性）。

## 4. 日志里的成功标志

```
PANDORA GPU: HFI perf table ACKED
PANDORA GPU: level 14 (user interface) done
[bo pool] used 1291/4096 580 MiB, free 548 MiB, extents 11/1024
PANDORA GPU UC: client 0 opened by backboardd through AGXAccelerator
VKTEST: device 'Adreno (TM) 840 (raytracing disabled)' ... api 1.4.354
VKTEST: part 3: pixels ffbf8040 ... 0 of 4096 wrong.
VKTEST: PASS (0 failures).
```

失败标志：

| 日志 | 含义 |
|---|---|
| `PANDORA GPU HANG` | BO 池 extent 打满（见 0226） |
| `method 2 -> 0xe00002bd` | BO 分配失败（同上） |
| `vkWaitForFences -> 2` | ring 写指针覆盖（见 0226 的 `ring_wait_space`） |
| `no-turnip ` / `LoaderNotFound` | Turnip 未装 / 未进信任缓存 |

## 5. 仍未解决：呈现（不是 GPU）

GPU 渲染已验证正确，但屏幕显示仍有：

| 现象 | 现状 |
|---|---|
| 锁屏/桌面壁纸纯色 | 未解决 |
| 设置里部分文字马赛克 | 未解决（与 `sweetfb_fill=1` 的最近邻放大有关，但 `fill=0` 时仍有） |
| 快速滑动画面异常 | 减轻（GPU 不再停住），未完全解决 |

已排除的假设：

- GPU 渲染错误 → vktest 像素 0 错，排除
- BO 泄漏 → `[bo pool]` 稳定，排除
- 逻辑屏改原生 1216×2688 → **iPhone X 固件黑屏**，不可用
- `pandora_scanout=1` → 画面无变化
- `sweetfb_fill=0` → 有黑边，马赛克仍在

下一步方向：CARenderServer 合成路径（`NativeAdrenoMetal.publish` 是否真正生效）、
present 的区域/同步，以及 CA 的 surface 格式（BGRA + bpr 4608 vs 面板 stride 4864）。
