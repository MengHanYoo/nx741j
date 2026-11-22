# 未完成项（2026-11-22）

状态口径：`完成` 只有在源码、构建和对应证据都存在时使用；
`主机完成` 表示本地构建或静态验证通过；`待真机` 表示当前没有设备可测；
`冻结` 是用户明确决定的暂缓项。

## 官方步骤对照

| # | 项 | 官方页 | 状态 | 还缺什么 |
|---|---|---|---|---|
| 1 | 固定设备输入、组件和构建树 | [移植步骤](/zh/porting/steps) | 完成 | `unixnu.lock`、本地 SoC/ShengUfs 源和资源路径已固定 |
| 2 | 共享支持与 OS ABI | [共享支持](/zh/porting/shared-support) | 完成 | 只有 iOS 16.7.16/20H392 被限定；未声明第二个 profile |
| 3 | 设备仓库与固件覆盖 | [设备仓库](/zh/porting/device-repository) | 完成 | `package/` 映射到 `kits/nx741j`；固件 overlay 可应用 |
| 4 | 配置解析、tree 和源码归档 | [构建](/zh/porting/building) | 完成 | `defconfig → tree → kernel → payload → firmware` 已实际跑通 |
| 5 | 硬件描述、ADT、UEFI | [设备树](/zh/porting/device-tree) | 完成 | ADT 有真实显示、UFS 和根 UUID；真机侧仍要观察 |
| 6 | 驱动适配顺序 | [驱动](/zh/porting/drivers) | 主机进行中 | 存储、显示、HID、CPU idle 已链接；其余见下表 |
| 7 | 启动与恢复 | [启动验证](/zh/porting/boot-verification) | QEMU 通过 | 无 ADB/9008，未刷最终镜像 |
| 8 | 记录和发布 | [记录](/zh/porting/records) | 进行中 | 本轮记录已更新；用户要求先不发布/不启用 Actions |

## 本机子系统

| 项 | 状态 | 当前证据/边界 |
|---|---|---|
| UFS adopt + 动态根锚点 | 主机完成，待真机 | `NXROOTP1`、GPT 边界、manifest SHA-256、`SHNGROOT` 解析均已实现；锚点未写入手机 |
| 真实 VerifiedRoot | 主机完成 | OS/SystemOS 全部块哈希通过 C/RSA 实现；最终 header-only 策略已编入 `Policy.o` |
| 正常 `native-ufs-root=1` | 构建通过 | 默认 payload 已切换；`sheng-pmgr=2` 通过 payload 构建器检查；真机能否挂根未验证 |
| PMGR | 主机 ABI 完成，运行待测 | `ShengPlatformPower` 340 项虚表审计通过；`_sheng_pmgr_run` 已链接；报告明确 `runtime_qualified=false` |
| 显示/亮度 | 源码完成，待真机 | `SweetFramebuffer`、`IOCoreSurfaceRoot`、`AppleCLCD`、`AppleARMBacklight` 已链接；QEMU 不模拟真实面板 |
| 触摸/HID/按键 | 源码完成，待真机 | Synaptics TouchCom、GPIO SPI、HID service、PON/GPIO101 worker 已链接；供电和 GPIO 状态未在手机验证 |
| CPU idle | 源码完成，待真机 | PSCI core retention state 0x4；连续拒绝会回退 WFI；调频 governor 未做 |
| APFS 选卷与 `launchd` | 待真机 | 根路径和 UUID 已准备；没有手机日志，不宣称进入 iOS |
| 持久 Data / 性能 | 冻结（用户要求） | 等能启动 iOS 后再做 |
| GPU/Metal | 未完成 | Adreno 840v2 仍需要 A740 接口适配、GMEM 和 Metal/Turnip 路径 |
| 电池/温控 | 未完成 | 需要真实 PMIC 电量和温度源 |
| USB/Wi-Fi/音频/基带/相机/背屏 | 冻结 | 第 7 步，等 iOS 启动后再做 |
| 独立 smoke 六场景 | 单独记录 | 直接 QEMU 引导门禁为绿；旧 AAVMF/TCG 偶发卡死没有继续修 |
| QR 固件日志导出 | 待真机 | 固件模块已具备，设备上的 Tools → Advanced Logger 需实机确认 |

## 当前阻塞

1. **没有 ADB/9008 设备**：不能写新根镜像、安装尾锚点、刷 FD/payload，
   因此不能宣称 APFS 挂载或 `launchd`。
2. **PMGR 运行资格**：源码和 ABI 已验证，但原厂 nub 发布、AMFI 依赖和
   电源拒绝路径只能在真机上完成。
3. **持久化**：用户要求第 7 步前冻结；当前不做 Data 分配、回写和性能测试。
4. **GPU 和温控**：SM8850 的 Adreno 840v2、频率和热管理还没有硬件闭环。

## 验证入口

```sh
# 干净重建
./unixnu defconfig nx741j_defconfig -D ../UniXNU-nx741j
./unixnu tree -D ../UniXNU-nx741j
./unixnu kernel -D ../UniXNU-nx741j -j 16
./unixnu payload -D ../UniXNU-nx741j
./unixnu firmware -D ../UniXNU-nx741j

# 只读诊断（不在正式 root payload 中）
./unixnu payload -D ../UniXNU-nx741j \
  --kernel-arg native-ufs-root=0 \
  --kernel-arg native-ufs-probe=12

# 直接 QEMU 引导链门禁
QEMU_MEM=16G QEMU_SMP=16 QEMU_INT=1 \
  ./scripts/qemu-nx741j.sh /path/to/UniXNU 60
```

最新详细验收边界见 [STATUS-2026-11-22.md](STATUS-2026-11-22.md)，
框架与产物索引见 [INTEGRATION-2026-11-22.md](INTEGRATION-2026-11-22.md)。
