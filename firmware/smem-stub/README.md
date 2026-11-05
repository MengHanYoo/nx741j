# SMEM 桩驱动（SmemStubDxe）

2026-11-05 加入。**第 2 层 SMEM 硬阻塞的解法**：不再让原厂 `SmemDxe` 去访问 `0x81D00000`
（BootShim 路径下该段没有 UEFI 页表映射，入口直接 `EFI_NOT_FOUND` + ASSERT），
改由一个自建 DXE 驱动安装同样的 `EFI_SMEM_PROTOCOL`，后备内存来自 DXE 自己的页。

## 结论（有真机证据）

- 桩驱动装入 FV、排在 `DALSYSDxe` 之前后，**SmemDxe 的 ASSERT 消失，DXE 链继续往下走**；
  卡点从 SMEM 推进到后续驱动（见 `docs/NX741J-START-HERE.md` 的最新状态）。
- 原厂 `SmemDxe` 从 FV 里移除（保留 INF/EFI 文件本身，未删）。
- 协议布局已从原厂 `SmemDxe.efi` 反汇编核对：`Revision=0x0000000000010001` + 4 个函数指针
  （`.data` 0x8098 起，重定位项 0x80A0/0x80A8/0x80B0/0x80B8）。
- 本固件里 SMEM 协议的唯一消费者是 `DALSYSDxe`（全 `Binaries/nx741j` 扫描，只有它和
  `SmemDxe` 引用 GUID `f4e5c7d0-d239-47cb-aacd-7f66ef763238`）。
- `DALSYSDxe` 只调用 `AllocFunc(item=404, size=8192, &p)`，并把该内存当自己的配置区
  （要求 4 KiB 对齐、内容为 0，随后自己写 magic `0xEF100DA1`）。桩驱动因此：
  - `AllocFunc`：按页分配、清零、页对齐；同一 item 复用、不足则重分配；
  - `GetFunc`：未分配时按需创建（避免调用方拿到 NULL）；返回实际大小；
  - `Func2/Func3`：返回 `EFI_SUCCESS`（本固件无调用方）。

## 文件

| 文件 | 作用 |
|---|---|
| `SmemStubDxe.c` / `.inf` | 驱动源码与模块定义（`FILE_GUID 6F1B2C4D-8E37-4A95-B0D2-5C7E9A13F648`） |
| `install.sh` | 把驱动装进固件树（`/opt/work/uefi-sheng-main`）并改 `DXE.inc`、`APRIORI.inc`、`nx741j.dsc` |
| `build.sh` | 直接调 BaseTools 增量构建（**不要用 `unixnu build firmware`**，它会去 GitHub 拉子模块，极慢） |
| `pack.sh` | `gzip(BootShim.bin + SILICIUM_UEFI.fd)` → Android header v4（无 DTB、ramdisk=0） |

## 固件树里的三处改动（`install.sh` 自动完成）

1. `Platforms/ZTE/nx741jPkg/Include/DXE.inc`：`SmemDxe` 行替换为 `SmemStubDxe`（原行注释保留）；
2. `Platforms/ZTE/nx741jPkg/Include/APRIORI.inc`：同样替换，位置不变（在 `DALSYSDxe` / `HWIODxe` 之前）；
3. `Platforms/ZTE/nx741jPkg/nx741j.dsc` 的 `[Components]` 增加 `nx741jPkg/Drivers/SmemStubDxe/SmemStubDxe.inf`
   —— 源码模块必须进 DSC，只在 FDF 里写会报 `NOT found in DSC file`。

## 构建（WSL，约 25 秒增量）

```sh
wsl -d Ubuntu -u root -- bash firmware/smem-stub/build.sh
```

关键环境（与 `unixnu` 的构建日志一致，见 `out/nx741j/.../logs/firmware.log` 头部）：

```
WORKSPACE=/opt/work/uefi-sheng-main
PACKAGES_PATH=$WORKSPACE:$WORKSPACE/Mu_Basecore:$WORKSPACE/Common/Mu:$WORKSPACE/Common/Mu_OEM_Sample:
  $WORKSPACE/Silicon/Qualcomm:$WORKSPACE/Silicon/Silicium:$WORKSPACE/Platforms/ZTE:
  $OUT/tree/package:$OUT/tree/drivers/DarwinBoot:/opt/work/refsrc/edk2
EDK_TOOLS_PATH=/opt/work/refsrc/edk2/BaseTools
CONF_PATH=$OUT/edk2-conf
PATH=$EDK_TOOLS_PATH/Source/C/bin:$EDK_TOOLS_PATH/BinWrappers/PosixLike:$OUT/toolchain:$PATH
PYTHONPATH=$EDK_TOOLS_PATH/Source/Python
-D FD_BASE=0xFF300000 -D FD_SIZE=0x400000 -D FD_BLOCKS=0x400 -D ENABLE_SECUREBOOT=0
```

产物：`Build/nx741jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`（4 MB，含 `FVMAIN` 9,105,408 B）。

## 打包 + 刷入

```sh
wsl -d Ubuntu -u root -- bash firmware/smem-stub/pack.sh boot-uefi-smemstub.img
# 产物 E:\default-workspace\z80u-collect\boot-uefi-smemstub.img，1,380,352 B = 337 扇区
powershell -File E:\default-workspace\z80u-collect\wsl\write_img.ps1 `
  -Img E:\default-workspace\z80u-collect\boot-uefi-smemstub.img -Sectors 337
```

刷入前确认设备在 9008（`Get-PnpDevice` 找 `Qualcomm HS-USB QDLoader 9008 (COMx)`）。
`write_img.ps1` 会做 Sahara → `Oem="ZTE"` configure → program `boot_a` @LBA 119078 → reset。
