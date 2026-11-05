# NX741J 任务 / 待办 / 问题归档（2026-11-06）

本文件是向 GitHub 推送的工作状态快照。详细技术记录见 `NX741J-WORKSPACE-INTEGRATION-2026-11-05.md`。

## 一、总体进度

| 层 | 状态 | 说明 |
|---|---|---|
| 1 启动镜像 | ✅ 完成 | `gzip(shim+FV)`，`ramdisk=0`，无 DTB，`text_offset=0xFF300000` |
| 2 UEFI 固件 | ⚠️ 进行中 | DXE 链推进到 `RpmhDxe` / `TzDxeLA`，被 ABL 环境缺失阻塞 |
| 3 UniXNU | ❌ 未上机 | `UniXNU.efi` 在 FV 内；payload 卷已生成 |
| 4 DarwinBoot | ❌ 未上机 | 二进制就绪 |
| 5 内核 | ❌ 未开始 | `out/.../kernel/` 不存在 |
| 6 用户态 | ❌ 未开始 | `out/.../userdata/` 不存在 |

## 二、已完成的工作（本轮）

### 2.1 三个固件桩驱动 / 补丁（真机验证）

| 项 | 文件 | 效果 |
|---|---|---|
| `SmemStubDxe` | `firmware/smem-stub/` | 替代原厂 SmemDxe，装 `EFI_SMEM_PROTOCOL`；DALSYS 通过（item 404 / 8192 B） |
| `KernelStubDxe` | `firmware/kernel-stub/` | 提供 `EFI_KERNEL_PROTOCOL`（v0x00010005，Lock/Event/Mutex/Sem/Spinlock 桩）；ChipInfo `spinlock.c+108` 消失 |
| Scheduler HOB 兜底 | `Silicon/Qualcomm/QcomPkg/Library/PlatformPeiLib/PlatformPei.c` | XBL 扫描为空时发布最小 `EFI_KERNEL_PROTOCOL`；DALSYS `AutoGen.c +382` 消失 |
| 内存映射 | `Platforms/ZTE/nx741jPkg/Library/MemoryMapLib/MemoryMapLib.c` | `SMEM Conv→Reserv`；新增 `RPMH_RSC 0x18900000/0x30000` |

### 2.2 配置与构建

- `unixnu.local.toml` 建立并验证（`status.build_config` 生效）
- `board.toml` 补齐 `[payload]`（timeout/tools/panic_screen/trust_cache_sha256）与 `[bootimg.image]`
- `payload.img` 打包成功（64 MiB，卷标 `NX741J`，`args = "-v serial=3"` 已用 `mtype` 在卷内验证）
- `dts/board.dts` 改为 `/include/ "sm8850.dtsi"` 结构（编译 1660 B，内容等价）
- `porting.toml [support].status` 更新为 `bringup-layer2-dxe-chain`
- `Kit.toml` 草案（`firmware/kit/`）：受上游 `payload.py` 硬编码 `autoload=false` 阻塞

### 2.3 关键诊断结论

- **入口 ABI 正常**：`gBS == SystemTable->BootServices == 0xC2120000`
- RpmhDxe 崩溃点：base `0xC1680000` / entry `0xC1690000` / ELR `0xC16900A4` → **RVA 0x10A4**（`ldr x3,[x2]`，x2=gBS+0xC0），FAR=0x58
- `TzDxeLA` 断言：`TzeLoaderDxe.c +1158`（Not Found）→ 复位循环；摘除后链路不再前进（无效果）
- **零可见性陷阱**：关掉 INFO 后 SEC/PEI 的 DEBUG 看不到，屏幕会停在旧画面
- 原厂 `uefi_a` 结构：FV 头在 0，SEC FFS @0xFE8（TE `ImageBase=0xC6801000`），主 FV @0x60538，尾部 0x2A8000–0x4FF000 为高熵内容（未解析）
- **原厂 FD_BASE = 0xC6800000**（用该基址重编后 SEC TE `ImageBase=0xC6800000`，与原厂同基址）

## 三、待办（按优先级）

| # | 任务 | 前置 | 备注 |
|---|---|---|---|
| 1 | 判定 ABL 加载契约：固定偏移跳转 vs 按 FV 解析 | — | 反汇编 `abl_a.img` 找 `#0xFE8` 立即数；或查 `xbl_config_a.img` 的 uefi_a 配置 |
| 2 | 解析 `uefi_a` 尾部 2.45 MB 高熵区 | — | 决定新镜像要写多长、是否能覆盖 |
| 3 | 换根：以 `FD_BASE=0xC6800000` 构建并写入 `uefi_a`（LBA 6） | 1、2 | 成功后所有桩可删；`boot_a` 留作回退 |
| 4 | 第 2 层收尾：Rpmh/Clock/UFS/BDS | 1–3 或继续补桩 | 当前卡点 |
| 5 | 交接二第 3 条：可读卷放 `\EFI\UniXNU\unixnu.toml` | payload 卷已备 | 计划 efisp FAT（LUN4 @748894） |
| 6 | 第 5 层：`[kernel.prelink]` + kernelcache 哈希 + `build kernel` | — | `0103` 已判定与上游同源 |
| 7 | Kit 激活 | 上游支持 | `payload.py:181` 硬编码 `autoload=false` |
| 8 | QEMU 验证通道 | 补 ArmVirtPkg | 当前不可用 |

## 四、已知问题清单

| 编号 | 问题 | 状态 |
|---|---|---|
| P0-1 | `0x81D00000` SMEM 内容全 0（XBL 数据不可见） | 未解，需自建 SMEM 镜像或换根 |
| P0-2 | DTB Extension 协议缺失（336 字节结构体） | 未解（HALIOMMU `Not Ready`、Mailbox `Locate DTB Failed`） |
| P0-3 | TZ 应用镜像（tzapps）缺失 | 未解（TzDxeLA 断言） |
| P1 | 无持久化运行时日志（DEBUG 只进 framebuffer） | 未解，可加内存日志缓冲 |
| P2 | `boot.json` 与手工 `pack.sh` 产物不一致 | 文档已标注 |
| P3 | 主仓库 `devices.toml` 未登记 NX741J | 待第 2 层跑通 |

## 五、产物与命令

```sh
# 构建（约 25 s，不经 GitHub）
bash firmware/smem-stub/build.sh
# 打包
bash firmware/smem-stub/pack.sh boot-uefi-<name>.img
# 刷入（需 9008）
powershell -File E:\default-workspace\z80u-collect\wsl\write_img.ps1 -Img <img> -Sectors <n>
```

| 产物 | 说明 |
|---|---|
| `boot-uefi-rpmh.img` | SMEM 桩 + Scheduler HOB + Kernel 桩 + RPMH 映射（链到 RpmhDxe） |
| `boot-uefi-landmark.img` | 当前 `boot_a` 上的版本（INFO 关 + 地标） |
| `firmware/smem-stub/`、`firmware/kernel-stub/` | 桩驱动源码与脚本 |
| `firmware/DRIVERS.md` | 驱动清单与接口约定 |
| `docs/NX741J-WORKSPACE-INTEGRATION-2026-11-05.md` | 全量技术归档（十八节） |
