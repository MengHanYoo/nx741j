# NX741J 新工作区起点（2026-11-05 收尾 / 23:00 更新）

## 一句话现状
第 1 层完成。第 2 层原来卡在 SMEM（HWIODxe -> DALSYSDxe -> SmemDxe -> SMEM 内存）；
**2026-11-05 晚已用自建 `SmemStubDxe` 绕过 SmemDxe，真机确认 ASSERT 消失、DXE 链继续往下走**，
现在卡点已推进到 SMEM 之后的驱动（以屏幕最后一行为准）。

## 已完成：SMEM 桩驱动

协议结构体（`Silicon/Qualcomm/QcomPkg/Include/Protocol/EFISmem.h`，已与原厂 `SmemDxe.efi` 反汇编核对）：

```c
#define EFI_SMEM_PROTOCOL_REVISION 0x0000000000010001
#define EFI_SMEM_PROTOCOL_GUID { 0xf4e5c7d0, 0xd239, 0x47cb, { 0xaa, 0xcd, 0x7f, 0x66, 0xef, 0x76, 0x32, 0x38 } }

typedef EFI_STATUS (EFIAPI *EFI_SMEM_ALLOCATE_ITEM)(int itemIndex, unsigned int size, void **pAddr);
typedef EFI_STATUS (EFIAPI *EFI_SMEM_GET_ITEM_ADDR)(int itemIndex, unsigned int* size, void **pAddr);
typedef EFI_STATUS (EFIAPI *EFI_SMEM_FUNC_2_3)(void* params);

struct _EFI_SMEM_PROTOCOL {
  UINT64 Revision;
  EFI_SMEM_ALLOCATE_ITEM AllocFunc;
  EFI_SMEM_GET_ITEM_ADDR GetFunc;
  EFI_SMEM_FUNC_2_3 Func2;
  EFI_SMEM_FUNC_2_3 Func3;
};
```

- 证据：原厂 `SmemDxe.efi` 的 `.data` 0x8098 起是 `0x0000000000010001` + 4 个重定位函数指针
  （0x80A0/0x80A8/0x80B0/0x80B8），入口用 `gBS->InstallMultipleProtocolInterfaces` 注册；
- 全 `Binaries/nx741j/**/*.efi` 扫描，只有 `SmemDxe`（生产者）和 `DALSYSDxe`（消费者）引用该 GUID；
- `DALSYSDxe` 实际只调 `AllocFunc(item=404, size=8192, &p)`，把返回内存当自己的配置区
  （要求页对齐 + 全 0，随后自己写 magic `0xEF100DA1`）。

桩驱动源码/脚本已归档到仓库：`firmware/smem-stub/`（`SmemStubDxe.c/.inf`、`install.sh`、`build.sh`、`pack.sh`、README）。

### 固件树三处改动（`install.sh` 自动做）
1. `Include/DXE.inc`：`SmemDxe` 行 → `SmemStubDxe`（原行注释保留）；
2. `Include/APRIORI.inc`：同样替换，位置不变（在 `DALSYSDxe`/`HWIODxe` 之前）；
3. `nx741j.dsc [Components]` 增加 `nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf`
   （源码模块必须进 DSC；只写 FDF 会报 `NOT found in DSC file`）。

### 构建（**不要**用 `unixnu build firmware`）
`./unixnu build firmware` 会先 `git fetch` GitHub 子模块，几分钟都下不来；直接调 BaseTools 增量构建只要 ~25 秒：

```sh
wsl -d Ubuntu -u root -- bash firmware/smem-stub/build.sh
# 产物 /opt/work/uefi-sheng-main/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd
```

### 打包 + 刷入
```sh
wsl -d Ubuntu -u root -- bash firmware/smem-stub/pack.sh boot-uefi-smemstub.img
powershell -File E:\default-workspace\z80u-collect\wsl\write_img.ps1 `
  -Img E:\default-workspace\z80u-collect\boot-uefi-smemstub.img -Sectors 337
```

## 关键路径

| 项 | 值 |
|---|---|
| 固件树 | WSL `/opt/work/uefi-sheng-main` |
| 设备仓库 | `E:\default-workspace\z80u-collect\repos\UniXNU-nx741j` |
| 当前 FV | `/opt/work/uefi-sheng-main/Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`（4 MB，FVMAIN 9,105,408 B） |
| shim | `/opt/work/uefi-sheng-main/BootShim/BootShim.bin`（192 B，FD_BASE=0xFF300000, FD_SIZE=0x400000, REQUIRES_KERNEL_HEADER=1） |
| 最近镜像 | `E:\default-workspace\z80u-collect\boot-uefi-smemstub.img`（1,380,352 B = 337 扇区，gzip(shim+FV)，ramdisk=0，无 DTB） |
| 设备备份 | `E:\Nubia_Flash_Brick\backup\ZTEToolBoxParBak_2026.10.24_22.38.43.421` |
| boot_a 恢复 | `write_img.ps1 -Img images\boot_a.img -Sectors 24576`（需 9008） |

## 已知结论（不要再试）

- 改 SMEM 属性/类型/顺序：无效
- ArmSetMemoryAttributes / gDS->AddMemorySpace：无效或崩溃
- 驱动打印/写盘抓日志：不可靠（logfs 服务抓不到 DEBUG；块设备句柄选不准）
- 换 SmemDxe 版本：uefi_a/uefi_b 相同，解锁版同样失败
- 去掉 SmemDxe(+DALSYS)：卡 HWIODxe / DALSYSDxe
- 把 FV 包进 uefi_a 的 ELF 头写入固件分区：黑屏
- **不要再让 `unixnu build firmware` 先跑 tree/fetch**：慢且无必要，见上面的 build.sh

## 显示参数（已确认）

GOP 必须 1216x2688（PcdFrameBufferWidth/Height），面板 1440x3200。

## 注意

- 不自动 git commit；
- 9008 会话用完需断电重进（NO free handles）；
- 中文路径不要传给 fh_loader；
- 9008 里设备是 `Qualcomm HS-USB QDLoader 9008 (COMx)`，`write_img.ps1` 自动找口。
---

## 2026-11-05 22:50 进展：SMEM 桩已验证，新卡点是 Scheduler HOB

### 已确认
- `SmemStubDxe` 被调度（屏幕出现 `loading driver smemstubdxe.efi`），装协议后 DXE 继续走；
- 之前看不到桩的打印是**日志级别**问题：本平台 `PcdDebugPrintErrorLevel = 0x8007EE0F`，**不含 DEBUG_INFO(0x40)**，
  所以桩里的 `DEBUG_INFO` 全部被过滤；已改成 `DEBUG_ERROR`（`0x80000000` 位一直会被打印）；
- 所以真机上 SMEM 协议大概率已装好，DALSYS 的 `Not Found` 不是 SMEM 引起的。

### 新卡点（反汇编证据）
`DALSYSDxe.efi` 的 AutoGen 包装（`_ModuleEntryPoint`）按顺序跑库构造，其中：

- `0x1c94`：`gBS->LocateProtocol(gEfiKernelProtocolGuid)`，失败则 `0x1d58` 读 **Scheduler Interface HOB**；
- `0x1dd0`：若调度器指针仍空 → **直接 `return EFI_NOT_FOUND`**；
- 包装在 `AutoGen.c +382` 对构造结果 `ASSERT_RETURN_ERROR` —— 与屏幕 `assert autogen.c +382: !return_error(status)` 完全对应。

Scheduler HOB 由 `Silicon/Qualcomm/QcomPkg/Library/PlatformPeiLib/PlatformPei.c:BuildXblHobs()` 建立，
而地址来自 `GetXblHobAddresses()`：它在 `FD_Reserved` 区扫 XBL 残留代码的机器码特征再解析 ADRP/ADD。
BootShim 直跳时扫不到 → `SchedulerInterfaceAddr = 0` → 不建 HOB → DALSYS 构造失败。

### 已改（`PlatformPei.c`）
`BuildXblHobs()` 里增加 `else` 分支：扫不到 XBL 调度器时，用 PEIM 内 `.data` 里的 0x60 字节全 0 桩表
（`mSchedulerStub`，首字节非 0 以保证落 `.data`；地址在 FD 0xFF300000/0x400000 内，DXE 仍映射），
只建 `gEfiSchedulerInterfaceHobGuid` 的 HOB，**不调用 `CreateFvList()`**（桩没有函数指针）。
DALSYS 只要求 HOB 存在且 `[addr+0x48]` 可读；该字段为 0 时它不会回调任何调度器函数。

### 待验证镜像
`E:\default-workspace\z80u-collect\boot-uefi-schedhob.img`（1,384,448 B = **338 扇区**），已构建，未刷入。
刷入后屏幕应出现（ERROR 级）：
```
SmemStubDxe: entry (gBS=0x...)
SmemStubDxe: EFI_SMEM_PROTOCOL install -> Success
SmemStub: item 404 -> 0x... (8192 bytes)
```
然后看 DALSYS 是否继续。
---

## 2026-11-05 23:10 进展：连续突破 3 个 DXE 阻塞

### 已通过（真机）

| 阻塞 | 证据 | 解法 |
|---|---|---|
| SMEM（SmemDxe 缺失） | `loading driver smemstubdxe.efi` → `install -> Success` → `SmemStub: item 404 -> ... (8192 bytes)` | `firmware/smem-stub/`：自建 `SmemStubDxe` 装 `EFI_SMEM_PROTOCOL`，item 表上限须 ≥ 405（DALSYS 要 item 404） |
| DALSYS `AutoGen.c +382` | 反汇编：构造函数 `0x1DD0` 读 `gEfiSchedulerInterfaceHobGuid`，HOB 不存在即 `return EFI_NOT_FOUND` | `PlatformPei.c:BuildXblHobs()` 加 else：XBL 扫描为空时用 PEIM `.data` 里 0x60 字节全 0 桩表建 Scheduler HOB |
| ChipInfo `spinlock.c +108` | ChipInfo 的锁走 `gEfiKernelProtocolGuid->Lock->InitRecursiveLock()`，BootShim 路径没有该协议 | `firmware/kernel-stub/`：`KernelStubDxe` 提供 Version=0x00010005 的 `EFI_KERNEL_PROTOCOL`，Lock/Event/Mutex/Sem/Spinlock 全桩 |

### 当前卡点：HALIOMMUDxe

屏幕：

```
loading driver HALIOMMU.efi
locate DTBExtnProtocol failed: not found
parseIORTDT: locate DTBExtnProtocol failed with error Not found
error image at 0x0C174E000 start failed: not found
```

- HALIOMMU 入口返回 `EFI_NOT_FOUND`（DXE 只记一条错误，继续调度）；
- 缺的是 **DTB Extension 协议**（GUID `0389B776-625F-11EB-83BE-C741A913DE34`）；
- 全 FV 扫描：**没有任何模块安装它**，但 26 个 vendor 模块在查它；
- 生产者是 **EnvDxeEnhanced.efi**（唯一同时引用该协议 GUID 和 `gEfiDtbExtensionHobGuid` 的模块）：
  - `0x2C58`：`GetFirstGuidHob(&gEfiDtbExtensionHobGuid)` → 取 HOB 数据（8 字节指针）→
    `memcpy(0x10500, ptr, 0x150)`（**336 字节结构体**）→ `InstallMultipleProtocolInterfaces(... 0x101D8, 0x10500)`
  - HOB 由 `PlatformPei.c` 在 `DtbExtensionAddr != 0` 时建，地址来自 `GetXblHobAddresses()` 的 XBL 残留扫描；
  - **EnvDxe 当前排在 HALIOMMU 之后**（APRIORI 57/58/59 附近，为满足 CfgUtil 依赖被移后），所以即使 HOB 有值也来不及。

### 下一步（按顺序）

1. 把 `DDRInfoDxe → CfgUtilDxe → EnvDxeEnhanced` 这一组整体前移到 `PlatformInfoDxe` 之后、`HALIOMMUDxe` 之前（保持三者内部顺序），让 EnvDxe 能在 HALIOMMU 之前装 DTBExtn；
2. 仍缺 DTB extension 的 336 字节结构体：XBL 侧产物。可先探测 `XBL_DT`（0x81A00000 / 0x60000）里是否有 FDT（magic `D00DFEED`）或该结构体；找到就把它的地址塞进 `gEfiDtbExtensionHobGuid`；
3. SMEM 读 0 的问题仍在：`SmemStub: read 0x81D0000 -> 00000000 00000000`（已把 MemoryMapLib 的 SMEM 从 `Conv` 改成 `Reserv`，仍为 0）。DT 里 `smem_region@81d00000` 带 `no-map`，地址正确，说明 XBL 数据没进到 UEFI 或已被清；等 HALIOMMU 之后回头查。
---

## 2026-11-05 23:25 探测结果与最新链状态

### XBL 内存探测（真机）

| 地址 | 内容 |
|---|---|
| 0x81A00000 | 非 0，ASCII 可读（`TOP_QUP_0_SE_2` 一类 DT 字符串） |
| 0x81A20000 | 非 0（同上，DT 字符串区） |
| 0x81A40000 | 全 0 |
| 0x81D00000 (SMEM) | 全 0 |
| 0x81C00000 (AOP) | **不可读：AP 侧读直接 Data Abort**（XPU 保护区，别碰） |
| 扫描 `d00dfeed`：XBL_DT(384 KB)、FD_Reserved(1 MB) | **各 0 命中** |

结论：XBL 的 DT 字符串区在 0x81A00000 附近，但**没有完整 FDT 头**；`FD_Reserved` 里也没有可用的 XBL 残留。
所以 `GetXblHobAddresses()` 拿不到 DTB extension / scheduler 是正常的，SMEM 也是空的。

### 链又推进了（真机）

```
loading driver HALIOMMU.efi
locate DTBExtnProtocol failed: not found
parseIORTDT: locate DTBExtnProtocol failed with error Not found
error image at 0x0C174E000 start failed: not found      ← HALIOMMU 只失败不致命
loading driver ulogdxe.efi  ... 一路到 loading driver rpmhdxe.efi
assert (ArmCpuDxe) CpuExceptionHandlerLib.c(438): ((BOOLEAN)(0=1))   ← 新卡点：异常
```

- HALIOMMU / ULogDxe / IPCCDxe / CmdDbDxe / PwrUtilsDxe / NpaDxe 都过去了；
- 卡在 **RpmhDxe**，现象是 CPU 异常（不是 ASSERT_RETURN_ERROR）；
- 怀疑 RPMh 访问的寄存器基址/通道配置来自 SMEM 或 DT（SMEM 现在全 0），也可能是 unmapped 寄存器。

### 下一步建议

1. 用 `ArmCpuDxe` 的异常打印确认异常类型/地址（Data Abort 还是 Prefetch）：如果是读某个基址，基本就是 SMEM/HWIO 数据缺失；
2. 优先解决 **SMEM 全 0**：XBL 明明初始化过，UEFI 侧却读到 0，可能被 PEI/SEC 或其它阶段清掉——可在 `BuildXblHobs` 里加一次“读前后对比”，或把 SMEM 从 `Reserv` 改成 `NoHob + INITIALIZED` 试试；
3. DTB extension：既然没有 FDT，只能走本地伪造 336 字节结构体（反汇编 HALIOMMU `parseIORTDT` 拿字段），或继续让 HALIOMMU 失败（不致命，先看后面还缺什么）。
