# 厂商 UFS 驱动反汇编对照（boot_a 提取的 vmlinux）

来源：`_tmp/vendor/vendor-kernel.elf`（65,894,248 B，从本机 `boot_a` 提取、kallsyms 已重建）。
符号齐备：`ufshcd_make_hba_operational`、`ufshcd_send_command`、`__ufshcd_send_uic_cmd`、
`ufshcd_hba_enable`、`ufshcd_config_mcq`、`ufshcd_mcq_make_queues_operational` 等。

## 1. HBA 初始化序列（`ufshcd_make_hba_operational` @ `0xffffffc080cd9cf0`）

| 顺序 | 寄存器 | 值 / 动作 | 说明 |
|---|---|---|---|
| 1 | `0x24` IE | 读回后 `|= 0x700a85` | 打开 UTP/UIC 中断（比我们多得多） |
| 2 | `0x4c` **厂商寄存器** | `(nutrs << 8) & 0x1f00 \| 0x81000002`；条件不满足时写 **0** | 位 8–12 放队列深度，固定位 `0x81000002`；语义待实测（见第 5 节） |
| 3 | `0x50/0x54` | UTRLBA / UTRLBAU | 传输请求队列基址（我们做了） |
| 4 | `0x70/0x74` | UTMRLBA / UTMRLBAU | **任务管理队列基址（我们没做）** |
| 5 | 读 `0x30` HCS | 要求 `HCS & 0xe == 0xe` | 三位都要 ready（UTRLRDY/UTMRLRDY/UCRDY） |
| 6 | `0x80` | 写 1 | **UTMRLRSR：任务管理队列 run（我们没做）** |
| 7 | `0x60` | 写 1 | UTRLRSR：传输队列 run（我们做了） |

`ufshcd_hba_enable`（`0xffffffc080cd9f50`）先读 `0x34` HCE，已置位就 `ufshcd_hba_stop`；
再 `ufshcd_crypto_enable` → `HCE = 1`（crypto 成功时写 **3**）→ 轮询 `HCE.bit0`；末尾 `IE |= 0x470` 后调 vops 钩子。

## 2. 门铃：厂商有两条路径（`ufshcd_send_command` @ `0xffffffc080ce2f1c`）

```
ldrb w8, [hba + 0x131d]      ; hba->mcq_enabled
cmp  w8, #1
b.ne legacy                  ; 否则走传统门铃
```

| 路径 | 写哪里 | 值 |
|---|---|---|
| **MCQ**（`mcq_enabled == 1`） | `[queue + 0x8]`（MCQ 空间的 SQ doorbell，来自 DTB `mcq_sqd` @ `0x1da5000`） | `(slot + 1) << 5` 后写入 queue 的门铃 |
| 传统 | `mmio + 0x58`（UTRLDBR） | `1 << tag`（与我们一致） |

传统路径在敲门铃前还会调 `hba->ops->?`（`[hba+0xa8]+0x58`，带 CFI 校验）——对应上游的
`setup_xfer_req`（Qcom 在这里做 ICE/crypto 与保护配置）。

字符串证据：`ufshcd_core.use_mcq_mode`（模块参数，**默认开启 MCQ**）、`MCQ configured, nr_queues=…`、
`MCQ mode is disabled, err=%d`、`failed to initialize (legacy doorbell mode not supported)`。

## 3. 与我们 `NativeUfs::adoptAndRead()` 的差异

| 项 | 厂商 | 我们 | 影响 |
|---|---|---|---|
| UTMRL 初始化（0x70/0x74/0x80） | 做 | **不做** | HCS 的 `UTMRLRDY` 永远不为 1；某些 HCI 版本在 UTMRL 未 run 时拒绝执行传输命令 |
| 厂商寄存器 0x4c | 写 | **不写** | 队列深度/模式未告知控制器，可能影响描述符取指 |
| IE 位 | `0x700a85` | 我们屏蔽中断（写 0x24=0） | 我们不靠中断，理论上无碍 |
| 门铃 | MCQ 时写 SQ doorbell | 只写 0x58 | **若固件已开 MCQ，0x58 可能无效甚至挂总线** |
| vops `setup_xfer_req` | 调用 | 不调用 | Qcom 在此做 ICE/保护；缺了通常报错而非挂死，但不能排除 |

## 4. 优先级假设

1. **H1（最高）：MCQ 已启用，传统门铃不是正确路径。** 厂商内核默认 `use_mcq_mode=1`，且本机 DTB 专门给了
   `mcq_sqd`（`0x1da5000`）与 `mcq_vs`（`0x1da4000`）两段——这是 MCQ 模式的证据。我们先要确认控制器当前是否
   在 MCQ 模式（读 `0x4c` 与 MCQ 空间的 `MCQCFG`），再决定：写 SQ doorbell，或显式关掉 MCQ 再走 0x58。
2. **H2：缺 0x4c 写。** 队列深度没设，控制器可能不认 UTRD。
3. **H3：缺 UTMRL 初始化。** 补齐 0x70/0x74/0x80 后再看 HCS 是否变成 `0xe`。
4. **H4：缺 vops `setup_xfer_req`（ICE）。** 优先级最低。

## 5. 第 2 步（Android 上 devmem 活参考）——**当前无法执行**

`adb devices` 为空：手机此刻不在 Android（或未连接）。连上后按下面取一次活参考：

```sh
adb root                      # 需要 root；没有 root 时用 dmesg + sysfs 只读信息
adb shell 'dmesg | grep -iE "ufshcd|mcq" | head -40'      # 先确认 MCQ 模式与队列数
adb shell 'ls /sys/devices/platform/soc/1d84000.ufshc/'    # 看 clock/ICE/power 属性
for r in 0x00 0x08 0x20 0x24 0x30 0x34 0x4c 0x50 0x54 0x60 0x70 0x74 0x80; do
  adb shell devmem $((0x1d84000 + r))
done
adb shell 'devmem 0x1da5000'   # MCQ 空间首字（判断 MCQ 是否启用）
```

判读：若 `dmesg` 出现 `MCQ configured` 或 `mcq` 字样 → 走 H1；若 `0x4c` 非 0 → 说明厂商标了队列深度；
若 `0x60/0x80` 与 `0x30` 的 ready 位对不上 → H3。

拿到这份 dump 后，再决定是“复刻 MCQ 门铃”还是“显式退出 MCQ 走传统门铃”。
