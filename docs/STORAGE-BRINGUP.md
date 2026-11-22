# UFS 存储 bring-up（ShengUfs 已接入构建）

## 状态

| 项 | 值 |
|---|---|
| 组件选中 | ✅ `ShengUfs` + `VirtioMmioBlock` |
| 内核配置 | ✅ `CONFIG_XNU_NATIVE_UFS=y` |
| 内核链接 | ✅ 无未定义符号，最终内核 sha256 `14e63ca1526f9b956fb9a41b33269b7a13390937a7a743bbba17009e9476207a` |
| payload / boot | ✅ 见构建输出 |
| 运行时 | ⚠️ 已启用默认参数，待真机：`native-source-storage=1 native-ufs-root=1 sheng-pmgr=2`；尚未刷机 |

2026-11-22 更新：真实 OS/SystemOS UUID、VerifiedRoot 双卷 header-only 策略和
`NXROOTP1` 动态锚点已编入；`native-ufs-noverify` 与固定 `nxroot-lba` 已移除。
当前主机产物见 `STATUS-2026-11-22.md`，真机 APFS 挂载仍未观察。

## 组成

| 文件 | 作用 |
|---|---|
| `ShengUfs-canoe`（本地覆盖） | `GccUfs.h` 换 canoe 分支偏移（AHB `0x77028`、UNIPRO `0x7706c`、ICE `0x7707c`、PHY_AUX `0x770bc`、RX0 `0x77030`、RX1 `0x770d8`、TX0 `0x7702c`、AGGRE `0x770f0`） |
| `build/storage-abi/IOBlockStorageDevice-reference.o` | 官方 `audit-storage-interface.py` 编译出的 IOStorageFamily ABI 参考对象（本机用 `llvm-dwarfdump` 与告警降级跑通前段） |
| `kits/VerifiedRoot/src/verified_root.c` + `adapters/xnu_crypto.c` | **真实校验实现**（`vr_open`/`vr_verify_block`/`vr_xnu_crypto`），随归档编译进内核 |
| `package/storage-bringup/Stubs.c` | 仅锚定 `verified_root_key`（395 B，零值）、策略表（count=0）与 `boot_progress_poll`；**不再提供空校验函数** |
| `scripts/build-storage-bringup.py` | 用内核编译记录编译 VerifiedRoot 与锚点，并把参考对象/对象成员 `llvm-ar r` **并入 `build/sheng-ufs/libShengUfs.a`**；无需修改内核构建脚本 |

## 已知限制

- verified-root **不再被绕过**：`vr_open`/`vr_verify_block` 为 VerifiedRoot 真实实现；策略表 count=0，
  因此校验**fail-closed**。
- 策略包本身已不需要外部输入：kit 自带 `tools/manifest.py`（签名）与 `src/verified_root.c`（校验），
  `scripts/mk-verified-root-policy.py` 已能在本机生成信封并用真实 C 实现验证通过（含篡改块、generation
  不匹配两个反例）。剩下的两件事是"信封怎么送进内核"（17 GiB dump 需要 128 MiB 哈希表，超出 64 MiB
  payload）与 xnu-8796 的 `source-storage-runtime.patch`。详见 `STORAGE-POLICY.md`。
- `IOBlockStorageDevice` 参考对象来自公开 Darwin 23 源码；`SourceStorage` 运行时会校验
  kernelcache 中基类大小（0x90），不匹配会 panic。
- 当前默认 payload 已带 `native-source-storage=1`、`native-ufs-root=1` 和
  `sheng-pmgr=2`；没有手机时仍不能把“会执行存储路径”当作已经挂载成功。
- 构建可复现：所有对象已并入 `libShengUfs.a`（该归档本就由 `XNU_NATIVE_UFS` 分支 force_load），
  不再依赖任何框架脚本补丁。
