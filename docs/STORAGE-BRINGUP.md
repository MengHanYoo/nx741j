# UFS 存储 bring-up（ShengUfs 已接入构建）

## 状态

| 项 | 值 |
|---|---|
| 组件选中 | ✅ `ShengUfs` + `VirtioMmioBlock` |
| 内核配置 | ✅ `CONFIG_XNU_NATIVE_UFS=y` |
| 内核链接 | ✅ 无未定义符号，`kernel.macho` sha256 `5b51a0558b38be5250e7a8ec12d70c988ced7dc336f2b41fffff6e656c6c3839` |
| payload / boot | ✅ 见构建输出 |
| 运行时 | ⚠️ 尚未启用：未加 `native-source-storage=1`，也未刷机 |

## 组成

| 文件 | 作用 |
|---|---|
| `ShengUfs-canoe`（本地覆盖） | `GccUfs.h` 换 canoe 分支偏移（AHB `0x77028`、UNIPRO `0x7706c`、ICE `0x7707c`、PHY_AUX `0x770bc`、RX0 `0x77030`、RX1 `0x770d8`、TX0 `0x7702c`、AGGRE `0x770f0`） |
| `build/storage-abi/IOBlockStorageDevice-reference.o` | 官方 `audit-storage-interface.py` 编译出的 IOStorageFamily ABI 参考对象（本机用 `llvm-dwarfdump` 与告警降级跑通前段） |
| `kits/VerifiedRoot/src/verified_root.c` + `adapters/xnu_crypto.c` | **真实校验实现**（`vr_open`/`vr_verify_block`/`vr_xnu_crypto`），随归档编译进内核 |
| `package/storage-bringup/Stubs.c` | 仅锚定 `verified_root_key`（395 B，零值）、策略表（count=0）与 `boot_progress_poll`；**不再提供空校验函数** |
| `scripts/build-storage-bringup.py` | 用内核编译记录编译 VerifiedRoot 与锚点，并把参考对象/对象成员 `llvm-ar r` **并入 `build/sheng-ufs/libShengUfs.a`**；无需修改内核构建脚本 |

## 已知限制

- verified-root **不再被绕过**：`vr_open`/`vr_verify_block` 为 VerifiedRoot 真实实现；当前未编译策略
  加载器且策略表 count=0，因此校验**fail-closed**。要真正挂载根卷，仍需生成并编译策略包
  （`build-sheng-source-storage.py` 或等价物）。
- `IOBlockStorageDevice` 参考对象来自公开 Darwin 23 源码；`SourceStorage` 运行时会校验
  kernelcache 中基类大小（0x90），不匹配会 panic。
- 运行时还需 `native-source-storage=1` 与 `native-ufs-root=1` 才会真正挂载；当前 bootargs 未加，
  因此即使刷入也不会执行存储路径。
- 构建可复现：所有对象已并入 `libShengUfs.a`（该归档本就由 `XNU_NATIVE_UFS` 分支 force_load），
  不再依赖任何框架脚本补丁。
