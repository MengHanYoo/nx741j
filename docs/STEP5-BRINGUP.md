# 阶段 5：内核入口 → 平台专家 → kext 引导

记录 2026-11-09 夜到 11-10 的定位过程：哪些假设被证伪、真因是什么、现在停在哪、下一步怎么走。

## 现在的位置

```
内核入口                ✅ 蓝带（stage-1）
MMU 打开                ✅ 品红带（stage-2）
pmap_bootstrap          ✅ 复位探针触发过
arm_vm_init 返回         ✅ 复位探针触发过
PE_create_console       ✅ 0002 之后通过（此前一直在这里 MemoryDump）
PE_init_platform(TRUE)  ✅ 3 秒复位探针确认
machine_startup         ✅ 3 秒复位探针确认
KLDBootstrap / kext 加载 ⏳ ← 现在这里
```

## 真因：BCM2837 的 `PLATFORM_PANIC_LOG_PADDR`

`pexpert/arm64/BCM2837.h` 把 `PLATFORM_PANIC_LOG_PADDR` 硬编码成 `0x3c0fc000`（树莓派的地址，
本机不存在）。`check_for_panic_log()` 在控制台创建之前就 `ml_io_map` 这个地址并读魔数，
于是每一次启动都在 `PE_create_console` 里面挂掉——和视频、串口、debug 都无关。

`patches/xnu/0002-bcm2837-drop-panic-log-paddr.patch` 去掉该宏后，设备树路径直接返回，
`PE_create_console`、`PE_init_platform(TRUE)`、`machine_startup` 依次通过（各自用 3 秒 PSCI 复位探针确认）。

## 四个已证伪的假设（都很贵，记下来免得再走）

### 1. 帧缓冲需要内核自己 1:1 映射 —— 错

`patches/xnu/0001-*.patch` 第一版在 `arm_vm_init` 里 `pmap_map_bd(v_baseAddr, …)`。
实际 `initialize_screen()`（`osfmk/console/video_console.c:2836`）自己就会映射：

```c
newVideoVirt = boot_vinfo->v_baseAddr;
makeMapping = (kernel_map == VM_MAP_NULL) || (0 != (1 & newVideoVirt));
if (makeMapping) {
    new_vinfo.v_physaddr = boot_vinfo->v_baseAddr & ~3UL;
    newVideoVirt = ml_io_map_unmappable(new_vinfo.v_physaddr, newMapSize, flags);
}
```

`PE_create_console` 运行时 `kernel_map` 还是 NULL，所以内核**一律把 `v_baseAddr` 当物理地址**。
加载器按 Apple 的协议给物理地址即可；改写这个字段会让内核把 VA 当物理地址去映射（黑屏 + 立刻 MemoryDump）。

### 2. 把 `v_baseAddr` 换成内核 VA 能修好控制台 —— 错

第二版（`virtBase + (pa - physBase)`）与第三版（`dynamic_memory_begin` 另映射）都试过，结果相同：黑屏 + 立刻 MemoryDump。

### 3. 视频控制台是死因 —— 错（最有价值的一步）

`no-video=1` 让 DarwinBoot 把 `Video[]` 全置 0，内核走 `"No video - forcing serial mode"`（不映射、不画字），
**内核照样立刻 MemoryDump**。这才把怀疑对象从视频路径挪到 `PE_create_console` 的非视频部分，最终落到 panic-log 地址。

### 4. 复位探针与彩带可以共存 —— 错

`patch-xnu-entry-probe.py` 的 stage 1/2 彩带在 **MMU 关闭**时写 `Video.v_baseAddr`：有视频（物理地址）时安全，
`no-video=1`（地址 0）时写 0 → 内核入口立刻中止。本地加 `UNIXNU_PROBE_NO_PAINT=1` 跳过彩带，只注入 stage-4 复位；
上游等价物是 `--no-paint` 参数。

## kext 阶段现在在打什么

过了 `machine_startup` 之后，崩溃点移到 kext 装载：

| 现象 | 处理 |
|---|---|
| 类重复注册（dupclass） | 二分哪些 kit 与内建类冲突 |
| `ArmPlatform` 初始化顺序 | 有/无 `ArmDTPlatform` 两组对照 |
| 未实现 prototype 的 selector | `protnoop` 补丁把未实现的调用变成 no-op 以便继续 |
| 常量写保护 | `constw` 补丁定位只读段写入 |
| `IOService::waitForService` 无界等待 | `waitdog` 补丁给等待加看门狗：缺失的 provider 会 panic 报名字，而不是静默挂住 |

## 内核补丁清单

| 补丁 | 作用 |
|---|---|
| `0001-arm-vm-init-*` | 保留作参考：自己映射帧缓冲是错的方向 |
| `0002-bcm2837-drop-panic-log-paddr` | **真正的修复**：去掉树莓派的 panic-log 地址 |
| `0003-skip-security-extensions` | 跳过 `KLDBootstrap::loadSecurityExtensions()`：AppleImage4 的 `start()` 因 AMFI early_security_init 未接而 panic（`failed to get crypto algorithm: 2`） |

## 下一步（按序）

1. **kext 阶段收尾**：把上面五类崩溃逐个消掉，直到 kext 装载完成、进入 `launchd` 之前的最后一段；
2. **阶段 6 存储**：`ShengUfs` 真接管（`native-ufs-root=1` + `sheng-pmgr=2`）→ 发布 userdata 的 `OS`/`SystemOS` 窗口 → `root_match_volume_uuid` → 挂根；
3. **显示**：固件侧已能画（菜单/彩带/panic 屏），内核侧显示与背光驱动待写；
4. **输入**：UEFI 按键仍阻塞在 TLMM 映射（`0x0F010000` 起读崩）。

## 影像记录（每次刷机的判据）

| 时间 | 配置 | 真机结果 |
|---|---|---|
| 11-09 23:23 | scratch-video VA + 彩带 | 入口 +0xA4 异常（彩带写 VA） |
| 11-09 23:31 | 面板 PA + 内核 VA 补丁 + 彩带 | 彩带 → 黑屏 → 立刻 MemoryDump |
| 11-09 23:40 | 面板 PA，无补丁，彩带 | 彩带 → 黑屏 → 立刻 MemoryDump |
| 11-09 23:45 | no-video，无补丁 | 无彩带 → 黑屏 → 立刻 MemoryDump（证伪视频假设） |
| 11-10 00:39–08:19 | 0002 之后（复位探针逐个前移） | `PE_create_console` / `PE_init_platform` / `machine_startup` 逐个确认；停在 kext 加载 |
