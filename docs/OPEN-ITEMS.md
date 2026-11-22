# 未完成项（2026-11-22）

状态口径：`完成` 只有在源码、构建和对应证据都存在时使用；
`主机完成` 表示本地构建或静态验证通过；`待真机` 表示当前没有设备可测；
`冻结` 是用户明确决定的暂缓项。

按官方八步等权估算，当前流程覆盖度约为 **80%**；最终“进入 iOS 桌面”
验收仍未通过。步骤 1–5 已完成，步骤 6 的存储/显示/输入/PMGR 已接入，
GPU 与电源后端仍缺，步骤 7 已按要求冻结，步骤 8 的本地整合和发布记录
已基本完成。

## 官方步骤对照

| # | 项 | 官方页 | 状态 | 还缺什么 |
|---|---|---|---|---|
| 1 | 固定设备输入、组件和构建树 | [移植步骤](/zh/porting/steps) | 完成 | `unixnu.lock`、本地 SoC/ShengUfs 源和资源路径已固定 |
| 2 | 共享支持与 OS ABI | [共享支持](/zh/porting/shared-support) | 完成 | 只有 iOS 16.7.16/20H392 被限定；未声明第二个 profile |
| 3 | 设备仓库与固件覆盖 | [设备仓库](/zh/porting/device-repository) | 完成 | `package/` 映射到 `kits/nx741j`；固件 overlay 可应用 |
| 4 | 配置解析、tree 和源码归档 | [构建](/zh/porting/building) | 完成 | `defconfig → tree → kernel → payload → firmware` 已实际跑通 |
| 5 | 硬件描述、ADT、UEFI | [设备树](/zh/porting/device-tree) | 完成 | ADT 有真实显示、UFS 和根 UUID；真机侧仍要观察 |
| 6 | 驱动适配顺序 | [驱动](/zh/porting/drivers) | 主机进行中 | 存储、显示、HID、CPU idle 已链接；其余见下表 |
| 7 | 启动与恢复 | [启动验证](/zh/porting/boot-verification) | QEMU 通过；真机到 NativeUfs | 复位后为 QUSB_BULK；需要再次进 9008 读取 probe=12 日志 |
| 8 | 记录和发布 | [记录](/zh/porting/records) | 进行中 | 本轮记录已更新；用户要求先不发布/不启用 Actions |

## 本机子系统

| 项 | 状态 | 当前证据/边界 |
|---|---|---|
| UFS adopt + 动态根锚点 | 已安装、正常根已修、待成功启动 | 根镜像和 `NXROOTP1` 已回读；正常根改为 `cold init → adopt bind → startRoot`，adopt 失败不再回退 host bring-up |
| 真实 VerifiedRoot | 主机完成 | OS/SystemOS 全部块哈希通过 C/RSA 实现；最终 header-only 策略已编入 `Policy.o` |
| 正常 `native-ufs-root=1` | 构建通过，待真机 | 默认 payload `8801fbd7…`；旧日志证明执行到 NativeUfs；新路径等待下一轮 9008 日志 |
| PMGR | 主机 ABI 完成，运行待测 | `ShengPlatformPower` 340 项虚表审计通过；`_sheng_pmgr_run` 已链接；报告明确 `runtime_qualified=false` |
| 显示/亮度 | 源码完成，待真机 | `SweetFramebuffer`、`IOCoreSurfaceRoot`、`AppleCLCD`、`AppleARMBacklight` 已链接；QEMU 不模拟真实面板 |
| 触摸/HID/按键 | 源码完成，待真机 | Synaptics TouchCom、GPIO SPI、HID service、PON/GPIO101 worker 已链接；供电和 GPIO 状态未在手机验证 |
| CPU idle | 源码完成，待真机 | PSCI core retention state 0x4；连续拒绝会回退 WFI；调频 governor 未做 |
| APFS 选卷与 `launchd` | 待真机 | 复位后停在 `QUSB_BULK_CID`；捕获日志已到 NativeUfs，但尚无 APFS/launchd |
| loader 进度记录 | 已修复并写入实机 | 旧 `gRawBase=0` 会定位到 `super` LBA `20224`；新 loader 显式绑定 rawdump 并启用 UFS snapshot，回读一致 |
| 持久 Data / 性能 | 冻结（用户要求） | 等能启动 iOS 后再做 |
| GPU/Metal | 未完成 | Adreno 840v2 仍需要 A740 接口适配、GMEM 和 Metal/Turnip 路径 |
| 电池/温控 | 未完成 | 需要真实 PMIC 电量和温度源 |
| USB/Wi-Fi/音频/基带/相机/背屏 | 冻结 | 第 7 步，等 iOS 启动后再做 |
| 独立 smoke 六场景 | 单独记录 | 直接 QEMU 引导门禁为绿；旧 AAVMF/TCG 偶发卡死没有继续修 |
| QR 固件日志导出 | 待真机 | 固件模块已具备，设备上的 Tools → Advanced Logger 需实机确认 |

## 当前阻塞

路线纠偏记录见
[OFFICIAL-ALIGNMENT-2026-11-22.md](OFFICIAL-ALIGNMENT-2026-11-22.md)。
当前主线不再继续混用 adopt firmware queue、标准 host queue 和 VerifiedRoot
policy table；iOS 16 存储将按 data-a-live 的固定 extent + HFS+ `ufsroot0`
路线重新移植。

1. **真机启动停在 QUSB_BULK**：旧日志最终明确停在 NativeUfs 的正常根
   路径；新 probe=12 payload 已写入，需再进 9008 读取日志。
2. **super 单扇区误写**：旧 p7 把 BP_RECORD 写到 LBA `20224`；已提供
   `scripts/host/repair-super-progress-sector.ps1`，从 2026-11-09 备份只恢复
   4 KiB，执行前会校验当前记录。
3. **PMGR 运行资格**：源码和 ABI 已验证，但原厂 nub 发布、AMFI 依赖和
   电源拒绝路径只能在真机上完成。
4. **持久化**：用户要求第 7 步前冻结；当前不做 Data 分配、回写和性能测试。
5. **GPU 和温控**：SM8850 的 Adreno 840v2、频率和热管理还没有硬件闭环。

真机写入和恢复步骤见
[DEVICE-TEST-2026-11-22.md](DEVICE-TEST-2026-11-22.md)。

## 验证入口

```sh
# 干净重建
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j
./unixnu tree -D ../UniXNU-nx741j
./unixnu kernel -D ../UniXNU-nx741j -j 16
./unixnu payload -D ../UniXNU-nx741j
./unixnu firmware -D ../UniXNU-nx741j
./unixnu bootimg -D ../UniXNU-nx741j

python3 scripts/check-bringup.py --tree out/nx741j/ios-16.7.16-20H392/nx741j/tree \
  --device ../UniXNU-nx741j

# 只读诊断（不在正式 root payload 中）
./scripts/build-payload-modes.sh /path/to/UniXNU

# 直接 QEMU 引导链门禁
QEMU_MEM=16G QEMU_SMP=16 QEMU_CORES=8 QEMU_THREADS=2 QEMU_INT=1 \
  ./scripts/qemu-nx741j.sh /path/to/UniXNU 60
```

最新详细验收边界见 [STATUS-2026-11-22.md](STATUS-2026-11-22.md)，
框架与产物索引见 [INTEGRATION-2026-11-22.md](INTEGRATION-2026-11-22.md)。
