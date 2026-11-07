# 启动链（本机实现 vs 官方 `concepts/boot-chain`）

官方启动链是四次交接、五个阶段。本机**第 2–5 阶段就是官方实现**（组件版本已对齐官方目录），
第 1 阶段有两套产物：官方 BootShim `boot.img`（能构建、结构已核对）与本机实际使用的 **efisp loader**。

## 阶段对照

| # | 官方阶段 | 本机实现 | 证据 |
|---|---|---|---|
| 1 | bootloader → BootShim | `scripts/unixnu/bootimg.py` 的 `compile_shim()` 用 `BootShim.S` 加 `-DFD_BASE=0xff300000 -DFD_SIZE=0x400000 -DREQUIRES_KERNEL_HEADER` 汇编，kernel 字段 = gzip(shim+FD) + DTB | 解析产物：gzip 解出 **4,194,496 B = shim 192 B + FD 4,194,304 B**，尾部 DTB `d00dfeed`（1,660 B），FD 首字 `0x14002754` |
| 2 | UEFI → UniXNU | 固件卷内 `UniXNUPkg/Driver/UniXNUDxe` 注册 `Boot####`（load option `unixnu:firmware`），BDS 启动 `UniXNU.efi`；`nx741j.fdf` 已包含这两个 INF | 源码：`UniXNUDxe.c` 的 `mFirmwareMarker = L"unixnu:firmware"`；**产物**：未压缩 `FVMAIN.Fv`（9,256,960 B）含 `UniXNU` GUID ×2、`UniXNUDxe` GUID ×1；压缩后的 `FVMAIN_COMPACT.Fv` = FD = 4,194,304 B（FD 字节里搜不到 GUID 是因为整卷 LZMA 压缩） |
| 3 | UniXNU → DarwinBoot | `UNIXNU_LOADER_BOOT_REQUEST`（Volume/KernelPath/BootArgs/DeviceTree/Ramdisk/Kexts）；DT 准备 6 步：取 DTB→复制留 64 KiB→全局补丁→菜单项补丁→`[entry.chosen]`→安装 `EFI_DTB_TABLE` | `DarwinBoot.c` 的 `UnixnuBoot` 入口、`Test/Validation.c` 的请求构造 |
| 4 | DarwinBoot → XNU | Mach-O 校验用原始常量：magic `0xfeedfacf`、cputype `0x100000c`（ARM64）、filetype 低 24 位 0/2、段命令 `0x19`（`LC_SEGMENT_64`）、入口命令 `0x5`（`LC_MAIN`，长度 288）；段不得超 `SIZE_256MB` 窗口且入口须在可执行段内；trust cache 校验、`DARWIN_BOOT_ARGS`（Darwin 23 布局）、关 MMU/缓存后 EL1 跳转 | `DarwinBoot.c` 的 `ParseKernelHeader()`（常量校验）、`WINDOW_SIZE`；`BootArgs.h` 的 `STATIC_ASSERT`：`Video`=0x28、`DeviceTree`=0x60、`CommandLine`=0x6c、`MemorySizeActual`=0x478、`sizeof`=0x480，与页面表格逐偏移一致 |
| 5 | XNU → 用户态 | `ArmDTPlatform` 要求 `/cpus`、`/arm-io`、`/defaults`、`/options` 与 `/chosen` 的 CHRP NVRAM proxy；ADT 生成时写入 `empty_nvram_proxy()` | `ArmDTPlatform.cpp` 的 `hasNVRAMProxy()` 与 `/chosen does not supply a usable CHRP NVRAM proxy` 日志 |

## 本机差异：第 1 阶段用 efisp 取代 BootShim

本机 ABL 拒绝我们打的 `boot.img`（历史上多次回退 fastboot / 触发 MemoryDump），因此实际引导路径是：

```
ABL → efisp loader（自研 PE，LUN4 LBA 748894）
    → 读 rawdump（payload.img @+0，FD @+64 MiB）
    → FD 拷到 0xFF300000
    → daifset 屏蔽中断、自有 32 KiB 栈、关 MMU/D-cache/I-cache、tlbi
    → br 0xFF300000，x0 = FDT（保留 BootShim 约定；固件实际用内嵌 FdtBlob）
```

对应官方页的"失败行为"一节：我们的第 1 阶段换了实现，但**交接契约与官方一致**——固件卷被放到它期望的
基址、在 EL1 进入 UEFI；`x0` 保留 BootShim 的传参约定，但本固件改用内嵌 `FdtBlob`，不读 `x0`（见下表）。第 2 阶段起（`UniXNUDxe` → `UniXNU` → DarwinBoot → XNU）
与官方完全相同。

| 项 | 官方 | 本机 |
|---|---|---|
| 第 1 阶段载体 | `boot.img` 的 kernel 字段（gzip 流） | `efisp` 分区的 PE（3 MiB 镜像） |
| boot.img 是否使用 | 是 | **否**（仅归档；ABL 拒绝） |
| shim | BootShim.S（192 B） | 无（loader 直接跳 FD） |
| 固件卷来源 | kernel 字段后半 | `rawdump` + 64 MiB |
| 设备树传递 | boot.img 尾部 DTB | loader 仍按 BootShim 约定在 `x0` 传 FDT，但**本固件不消费**：DTB 来自 FDF 内嵌的 `FdtBlob`（FREEFORM GUID `25462CDA-221F-47DF-AC1D-259CFAA4E326`，UI `DeviceTreeBlob`），由 `DtPlatformDxe` 安装成 `gFdtTableGuid` |

## 各阶段验证状态

| 阶段 | QEMU | 实机 |
|---|---|---|
| 1（BootShim 结构） | — | ✅ 结构核对（上述 gzip/shim/FD/DTB 数字） |
| 1'（efisp loader） | — | ✅ **已验证**：P5 loader 从 efisp 启动固件，屏幕出现完整 DXE 日志（UFS/Pmic/Usb/Graphics/`EFI_KERNEL_PROTOCOL`） |
| 2（UEFI→UniXNU） | ✅ smoke 场景 1/2/3 | ⏳ 固件在 `DALSYS.efi` 后崩溃，尚未进入 UniXNU 菜单 |
| 3（UniXNU→DarwinBoot） | ✅ smoke 场景 5（loader 协议 v1/v2、kext 校验） | ⏳ 取决于阶段 2 |
| 4（DarwinBoot→XNU） | ✅ 合成载荷（QEMU 中协议与拒绝路径） | ⏳ 取决于阶段 2/3 |
| 5（XNU→用户态） | — | ⏳ 未开始（需要先过 4） |

## 失败行为对照（官方页 → 本机）

| 阶段 | 官方结果 | 本机现状 |
|---|---|---|
| bootloader | header 不匹配则拒绝 | 已证实：ABL 拒绝 boot.img，回退 fastboot/MemoryDump |
| 固件 | 无 GOP 或无输入则 headless/文本 | 固件崩溃在 `DALSYS.efi` 后（见 `ANALYSIS-EFISP-VENDORBOOT.md` §18） |
| UniXNU | 无配置则内置配置 + 可移动加载器 | QEMU 场景 3 已验证 |
| DarwinBoot | 内核/ADT/trust cache 无效 → `EFI_COMPROMISED_DATA` | QEMU 场景 5 的拒绝路径已验证 |
| XNU | 缺平台/存储驱动则 panic 给出缺失 provider | 未到该阶段 |

## 维护规则

- 改 `bootimg.py`/`BootShim`/`nx741j.fdf` 后，用 `scripts/unixnu/bootimg.py` 重新打包并按上面的解析方法复核
  `shim 192 B + FD 4,194,304 B + DTB`；
- 改 `efisp` loader 后必须同步 `loader/README.md` 的写盘顺序与回滚说明；
- 第 2–5 阶段的改动以官方组件版本为准（见 `components.toml`），本地只允许改设备配置，不改交接协议结构。

## 复核记录（第二轮）

| 项 | 结论 |
|---|---|
| `DARWIN_BOOT_ARGS` 布局 | 与页面表格逐偏移一致，`BootArgs.h` 用 `STATIC_ASSERT` 在编译期强制（0x28/0x60/0x6c/0x478/0x480） |
| 固件卷是否真的含 UniXNU | 含：未压缩 `FVMAIN.Fv` 中 `UniXNU` GUID ×2、`UniXNUDxe` GUID ×1；FD 是 LZMA 压缩后的 `FVMAIN_COMPACT.Fv`（4,194,304 B），所以直接搜 FD 字节得到 0 |
| efisp → 固件的设备树 | **修正**：loader 的 `x0=FDT` 只是保留 BootShim 兼容，本固件不读它——DTB 由 FDF 内嵌 `FdtBlob`（`DtPlatformDxe` → `gFdtTableGuid`）提供；XNU 用的 ADT 由 payload 从 `dts/board.dts` 生成，二者互不影响 |
