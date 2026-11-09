# 阶段 5：内核入口 → 平台专家（进行中）

记录 2026-11-09 一夜的定位过程：哪些假设被证伪、现在卡在哪、下一步怎么走。

## 现在的位置

```
内核入口            ✅ 蓝带（多次）
MMU 打开            ✅ 品红带
pmap_bootstrap      ✅ 复位探针触发过
arm_vm_init 返回     ✅ 复位探针触发过
PE_create_console   ❌ 之后的探针始终不触发
PE_init_platform    ⬜
machine_startup     ⬜
```

## 三个已证伪的假设（都很贵，记下来免得再走）

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
加载器按 Apple 的协议给物理地址即可；任何改写这个字段的做法都会让内核把 VA 当物理地址去映射
（表现为：清屏变黑，随即 MemoryDump）。

### 2. 把 `v_baseAddr` 换成内核 VA 能修好控制台 —— 错

第二版（`virtBase + (pa - physBase)`）与第三版（`dynamic_memory_begin` 另映射）都试过，
结果相同：黑屏 + 立刻 MemoryDump。原因同上。

### 3. 视频控制台是死因 —— 错（这是今晚最有价值的一步）

`no-video=1` 让 DarwinBoot 把 `Video[]` 全置 0，内核走 `"No video - forcing serial mode"`
分支（不映射、不画字，`v_depth=0`）。屏幕上只剩 `DARWINBOOT: hand-off` 与 `[15] jumpok`，
**内核照样立刻 MemoryDump**。

结论：死因不在视频/帧缓冲这条路径上，而在 `PE_create_console` 之后（或其非视频部分），
候选：`PE_init_printf(FALSE)`、`cpu_machine_idle_init(TRUE)`、`PE_init_platform(TRUE)`。

## 头文件层面的一个坑：探针与视频互相排斥

`scripts/patch-xnu-entry-probe.py` 的 stage 1/2 彩带在 **MMU 关闭**时写 `Video.v_baseAddr`：

- 有视频（物理地址）时安全；
- `no-video=1`（地址 0）时会写地址 0 → 内核入口立刻中止。

因此加了本地开关 `UNIXNU_PROBE_NO_PAINT=1`（构建时导出）：跳过彩带，只注入 stage-4 复位，
这样"无视频 + 复位探针"可以共存。这个开关只改本地工具树，尚未进上游；
上游需要的等价物是 `--no-paint` 参数。

## 下一步（按序）

1. 刷 `_tmp/payload-headless-probe.img`（无视频 + `PE_create_console` 之后 3 秒复位）：
   - 复位 → 控制台返回了，探针前移到 `PE_init_platform`（`_arm_init` 第 2 次调用）；
   - 不复位 → 崩溃在 `PE_create_console` 内，改用 `--in _PE_create_console` 的调用点二分。
2. 若能到 `PE_init_platform`，继续 `machine_startup`；之后才是阶段 6 的存储/根卷。

## 影像记录（每次刷机的判据）

| 版本 | 配置 | 真机结果 |
|---|---|---|
| 23:23 | scratch-video VA + 彩带 | 入口+0xA4 异常（彩带写 VA） |
| 23:31 | 面板 PA + 内核 VA 补丁 + 彩带 | 彩带 → 黑屏 → 立刻 MemoryDump |
| 23:40 | 面板 PA，无补丁，彩带 | 彩带 → 黑屏 → 立刻 MemoryDump |
| 23:45 | no-video，无补丁 | 无彩带 → 黑屏 → 立刻 MemoryDump（**证伪视频假设**） |
