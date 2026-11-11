# 会话轮数与步骤归档

- 会话轮数：第 **498** 轮（截至 2026-11-11 14:26，Asia/Shanghai）
- 工作区：`E:\default-workspace\z80u-collect`（设备仓库 `repos\UniXNU-nx741j`）
- 当前阶段：**第六步（驱动适配）· 显示子系统 · phandle 映射**

## 阶段划分（按轮次区间）

| 轮次区间 | 阶段 | 主要产出 |
|---|---|---|
| 1–120 | 步骤 1–6 审计与补全 | 设备输入、共享支持 pin、仓库布局、构建配置、DTS/ADT/固件、驱动矩阵 |
| 120–250 | 层 2–3 打通 | UEFI 4 MiB FD、DXE/BDS、UniXNUDxe 直启、菜单稳定 |
| 250–400 | 层 4 调试（V68→V77） | DarwinBoot 回调、`kernel/adt` 解析、停机根因（变量写、长 Stall、DeviceTree 非空） |
| 400–455 | 存储/驱动与验证 | ShengUfs 链接、VerifiedRoot、驱动检查清单、效率审计 |
| 455–480 | 9008/AVB 危机与恢复 | efisp 清零实验、patched ABL、2.0.15/2.0.26 回刷、最终 HyperOS 4.0 基线 |
| 480–493 | 进度记录与 P7 | P7 loader（`b1abc16a…`）、进度扇区机制、LogDumpDxe 移除、FD V85 `6f8b002c…` |
| 494–498 | 第六步·显示 | 实机 FDT 提取、BF375 面板 DTSI、GOP 占位节点、mdss 节点提取、phandle 映射 56% |

## 关键步骤（可复现）

1. **构建**：`cd /opt/work/nx741j-build/UniXNU && ./unixnu build firmware -D ../UniXNU-nx741j`
2. **kernel**：`./unixnu kernel -D ../UniXNU-nx741j`（当前 sha `d472c128…`）
3. **9008 写入**：FD LUN0 4760360（4 MiB）、efisp LUN4 748894（3 MiB）、payload 486d93f1…
4. **进度扇区**：LBA 4764200，`DBD2` 结构（Step/Status）
5. **实机 FDT**：`adb pull /sys/firmware/fdt` → `dtc -I dtb -O dts`

## 当前阻塞

1. 层 4：进度记录写入 rawdump 全零（UniXNU/DarwinBoot 侧 writer 未落盘）
2. 层 5：XNU 未启动
3. 显示：phandle 映射 56%（256/456），需属性白名单 + 补 label
4. 触摸：dtbo 非标准格式，节点未取到
5. 存储：缺 iOS 16 verified-root 策略包

## 下一步

1. phandle 白名单替换器 → `nx741j-mdss.dtsi` → `unixnu kernel` 编译
2. 实机 ADT dump 验证 framebuffer
3. 修层 4 进度写入，定位 DarwinBoot 断点
4. 触摸 TCM 组件
