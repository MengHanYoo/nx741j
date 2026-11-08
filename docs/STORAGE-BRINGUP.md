# UFS 存储 bring-up（ShengUfs 已接入构建）

## 状态

| 项 | 值 |
|---|---|
| 组件选中 | ✅ `ShengUfs` + `VirtioMmioBlock` |
| 内核配置 | ✅ `CONFIG_XNU_NATIVE_UFS=y` |
| 内核链接 | ✅ 无未定义符号，`kernel.macho` sha256 `ec801093463f93519bfc76d620edf933065824f4d2f5a813b0b94cff5a817eef` |
| payload / boot | ✅ 见构建输出 |
| 运行时 | ⚠️ 尚未启用：未加 `native-source-storage=1`，也未刷机 |

## 组成

| 文件 | 作用 |
|---|---|
| `ShengUfs-canoe`（本地覆盖） | `GccUfs.h` 换 canoe 分支偏移（AHB `0x77028`、UNIPRO `0x7706c`、ICE `0x7707c`、PHY_AUX `0x770bc`、RX0 `0x77030`、RX1 `0x770d8`、TX0 `0x7702c`、AGGRE `0x770f0`） |
| `build/storage-abi/IOBlockStorageDevice-reference.o` | 官方 `audit-storage-interface.py` 编译出的 IOStorageFamily ABI 参考对象（本机用 `llvm-dwarfdump` 与告警降级跑通前段） |
| `package/storage-bringup/Stubs.c` | 链接期最小 stub：`vr_open`/`vr_verify_block`/`vr_xnu_crypto`/`verified_root_*`/`boot_progress_poll` |
| `scripts/build-storage-bringup.py` | 组装 `build/storage-bringup/libStorageBringup.a` |
| 本地框架补丁 | `scripts/build-xnu-4k-baseline.sh` 在 `XNU_NATIVE_UFS=1` 时追加 `-force_load libStorageBringup.a`（框架树，未随设备仓库发布） |

## 已知限制

- `vr_*` 为**空 stub**：verified-root 策略校验被绕过，只能用于 bring-up；正式启用前必须换成
  私有 storage policy 归档（`build-sheng-source-storage.py`）或匹配的 VerifiedRoot 实现。
- `IOBlockStorageDevice` 参考对象来自公开 Darwin 23 源码；`SourceStorage` 运行时会校验
  kernelcache 中基类大小（0x90），不匹配会 panic。
- 运行时还需 `native-source-storage=1` 与 `native-ufs-root=1` 才会真正挂载；当前 bootargs 未加，
  因此即使刷入也不会执行存储路径。
- 复现构建需在框架树的 `build-xnu-4k-baseline.sh` 中保留该 force-load 片段（或用设备仓库补丁
  重新应用）。
