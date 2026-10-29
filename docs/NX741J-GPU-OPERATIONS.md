# NX741J（努比亚 Z80 Ultra）GPU 操作手册

面向「从零复现当前最佳状态」和「继续调试」两种场景。
所有数值来自手机本身的 DT / 固件，不是借用其它机型。

## 0. 当前最佳组合（2026-10-30 00:25）

| 组成 | 取值 |
|---|---|
| 内核 | 本仓库补丁 0225–0231 + `wsl/SweetFramebuffer.cpp` 基线 |
| 启动参数 | 见第 2 节 |
| 系统镜像 | `iosroot-SEAL.img`（插件已封装、Turnip 在 bundle 内、vktest 在 `/usr/bin`） |
| 已知最佳镜像名 | `METAL34-boot_a.img` |

状态：能开机、文字可渲染、GPU 卡死后 **5 秒自动恢复**；未解决：滑动文字模糊、快速滑动撕裂、偶发发热。

## 1. 补丁

| 补丁 | 作用 |
|---|---|
| `0225-nx741j-gpu-opp-from-dt.patch` | GPU OPP 17 档（160–1050 MHz）、GMU 475/700、bus 投票，全部来自 DT |
| `0226-nx741j-gpu-ring-space-and-pool.patch` | `ring_wait_space()` 防写指针覆盖；BO 池 extent 512→1024；`[bo pool]` 诊断 |
| `0227-nx741j-gpu-progress-marker-log.patch` | 插件进度标记 `PANDORA GPU MARK: 0x4147xxxx` |
| `0229-nx741j-gpu-open-type-and-queues.patch`（累计） | 接受 `open type 0x4148`；实现 `CreateQueue`/`CloseQueue`；`[gpu client text]` |
| `0230-nx741j-gpu-clients-and-single-thread.patch` | GPU 客户端槽位 4→12（含 32 位窗口表夹取） |
| `0231-nx741j-gpu-hang-recovery.patch`（累计） | 5 秒无进度 → 给所有 client 置 reset，下次提交带 `CP_RESET_CONTEXT_STATE` + LRZ/CCU invalidate |

> 0229 / 0231 是**累计**补丁（相对仓库里的 `wsl/*.c*` 基线），应用时用最新的即可。

## 2. 启动参数（实测）

```
debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress
rd=ufsroot0 ufsrw=1 rootrw=1
unentitled_ios_sim_launch=1 ignition_level=1 launchd_unsecure_cache=1
wdt=-1 wdt_test=no_user_checkin
sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=0 pandora_touch_gpio117=0
pandora_gpu=15 pandora_gpu_metal=1
sweetsample=lsd,SpringBoard
```

可选：

- `sweetfb_doublebuffer=1`：双缓冲骨架（实测**不能**消除撕裂，默认关闭）
- `sweetexecswap=bluetoothd:/usr/bin/vktest`：开机跑 vktest（需 `com.apple.bluetoothd` 在 disabled.plist 里为 `false`）
- `pandora_fbdump=20`：帧转储（写入日志转储区，实测未生效，保留备查）

## 3. 构建流程（WSL）

```
# 内核对象（改过的文件必须显式列出，否则会从 CI 对象取旧版）
bash /root/local_build.sh 2 /opt/work/out-GPU <files...>
# 预链接
python3 /opt/work/tools/mkprelink.py /opt/work/out-GPU/kernel.development.pandora \
        /opt/work/ipsw/kernelcache.arm64 /opt/work/plk/kNN-amfi.macho
# 负载
D22AP_ADT=... KERNEL=/opt/work/plk/kNN-amfi.macho TRUSTCACHE="..." ADTFLAGS="..." \
  bash wsl/make_phone_payload_nx741j.sh /opt/work/run1 "<boot args>"
# 镜像
rebuild_loader.bat  →  tools/mkbootimg_v4.py out/Image out/loader-boot.img
```

刷入：`adb push` + `su -c 'dd if=... of=/dev/block/by-name/boot_a bs=1M conv=fsync'`，**刷前必须 `stat -c%s` 确认大小 > 50 MB**。

## 4. 镜像内必须存在的文件

```
/System/Library/Extensions/NativeAdrenoMetal.bundle/
    NativeAdrenoMetal            带平台标志的 ad-hoc 签名，cdhash cbce9ba2…
    Info.plist, _CodeSignature/CodeResources
    libvulkan_freedreno.dylib    Turnip，cdhash 899d39cd…
/System/Library/Extensions/NativeAdrenoMetal.clients    backboardd（发布模式下只留它）
/System/Library/Extensions/NativeAdrenoMetal.publish    （发布模式）
/usr/bin/vktest                                          cdhash 5bdf5dbd…
/private/var/mobile/Library/Preferences/com.apple.backboardd.plist
    CAEnableDeepFramebuffer / CAEnableFramebufferCompression / CAEnableIOSurfaceCompression = false
```

信任缓存必须**同时**包含插件、Turnip、vktest 三个 cdhash（静态模块共 2763 条）。

## 5. 验证手段

| 手段 | 命令/日志 |
|---|---|
| 已知图像（最权威） | `sweetexecswap` 跑 vktest → `VKTEST: PASS (0 failures)`、`part 3: 0 of 4096 wrong` |
| 插件进度 | `PANDORA GPU MARK: 0x4147xxxx`（低 8 位即 ASCII 字符） |
| 插件文本 | 标记流：`0x7fff` 分隔行、`0x7000|ch` 每字符（解码脚本见 `tools/`） |
| 客户端 | `PANDORA GPU UC: client N opened by <进程>`、`open type N refused` |
| 悬挂 | `PANDORA GPU HANG: fence …`、`PANDORA GPU RECOVER: 5 s without progress` |
| 内存 | `[bo pool] used N/4096 …` |

## 6. 已证伪的假设（不要再走）

| 尝试 | 结果 |
|---|---|
| `pandora_scanout=1` | 更差：图标不绘制、黑块、下拉闪屏 |
| 逻辑屏 1216×2688 | iPhone X 固件黑屏 |
| `sweetfb_fill=0` | 黑边，马赛克仍在 |
| `pandora_tu_debug=nolrz,nocb` | 无变化 |
| 强制 CP thread 1 | **无法开机**（thread 3 必需，已回退） |
| `clients=*` / 纯 publish | 无变化 |
| `sweetfb_doublebuffer=1` | 撕裂无改善（撕裂不来自面板半写） |
| 帧转储 `pandora_fbdump` | 转储区始终为空 |

## 7. 下一步（按优先级）

1. **文字模糊**：用 CI（macOS + codesign）构建的插件替换本地 Linux 构建（本地未定义 `NDEBUG`，文档指出会影响 Mesa 行为）。
2. **撕裂**：把整帧推送到 DSI 的动作对齐到 vsync 边沿之后。
3. **发热**：CP 偶发停住，需要核对 CP 状态切换（`CP_THREAD_CONTROL`/`CP_SET_MARKER`）与固件期望。
