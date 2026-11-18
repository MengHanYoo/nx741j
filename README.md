# UniXNU-nx741j

**English** | [简体中文](README.zh-CN.md)

Device repository for the **Nubia Z80 Ultra (NX741J)** — Qualcomm **SM8850** (Kaanapali,
codename `canoe`), 8 cores, Adreno 840, UFS, panel `bf375`/`rm692h5` 1440×3200.

The port boots Apple's XNU through the official UniXNU chain:

```
bootloader -> efisp loader -> UniXNU (UEFI) -> DarwinBoot -> XNU -> userland
```

Target: **iOS 16.7.16 (20H392)**, XNU **8796.141.3**, arm64, 4 KiB pages. The OS profile is pinned
in [board.toml](board.toml); it is the only IPSW available for this port.

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | board description, components, build inputs | done |
| 2 | firmware (UniXNU UEFI app, DarwinBoot) | done |
| 3 | payload (kernel, ADT, trust cache) | done |
| 4 | boot chain hand-offs (loader -> UEFI -> DarwinBoot -> XNU) | done |
| 5 | kernel bring-up (platform expert, IOKit, prelinked kexts) | **done** |
| 6 | storage (UFS) | **blocked - platform (XBL/TZ/xPU)** |
| 7 | userland (root volume, `launchd`) | blocked by stage 6 |

**Stage 5 milestone (2026-11-12).** The kernel console now renders on the panel, so every boot stage
is observable on the device instead of a colour-bar screen:

```
Registered class IOResources.
...
Starting from prelinked kernel.
kext com.apple.kpi.bsd Loaded.
...
loading dependencies com.apple.kpi.mach
```

**Stage 6 (UFS).** `NativeUfs` matches `/arm-io/ufshc` and now runs the official takeover path
(`native-ufs-probe=3`): it stops the firmware request list, zeroes only the slots it uses, forces
stream `0x60` to bypass in `apps_smmu` and restarts the list at the firmware's own `UTRLBA` instead of
installing a kernel buffer.

The first attempt at that path died inside `NativeUfs::adoptAndRead+0x844` with a level-3 translation
fault (`far` = mapping base + `0x4000`, `esr=0x96000047`): the driver maps the request list with a
16 KiB window but cleans 32 KiB. `scripts/patch-native-ufs-adopt-fix.py` narrows the clean to the
mapped window, and the compiled constant is verified with `llvm-objdump`
(`+0x838: add x9, x9, #0x4, lsl #12` = `0x4000`) before any flash.

The vendor cold-init path still exists behind `native-ufs-cold-init=1` (PHY calibration tables,
SERDES start, PCS ready, HCE, DME link startup), but the official flow takes the firmware state over
instead of resetting the PHY, so it is off by default.

## Session log — 2026-11-12

### Kernel console log channel

The panel shows the kernel log but cannot be scrolled or copied, and this board has no UART, so
the console is mirrored into DRAM and collected on the next boot:

| Layer | What it does |
|---|---|
| `patches/xnu/0021-console-log-capture.patch` | `osfmk/console/serial_general.c` appends every `console_printbuf_putc` byte to a 128 KiB buffer; the `NXLOG1` magic is written on the first character, because the buffer lives in `__BSS` and a static initialiser is never copied into the window |
| `loader/p7.c` | scans DRAM for the magic, writes a status record (`NXLGST1` + found PA + length + 64 bytes) to `rawdump` LBA `4764464` and the log to `4764465`; log writes go through a 4 KiB aligned bounce buffer because `BlockIo` rejects the unaligned `found+12` source |
| `wsl/flash-boota-9008.ps1 -ReadTo` | reads any partition back over 9008 (`-ReadSectors`), the only way to look at those records |

Constraint: the buffer only reaches DRAM on the boot that produced it, and entering 9008 re-initialises
DDR and wipes it. The capture therefore needs one warm reset after the kernel has printed — which is
exactly what the boot-menu keys are needed for.

### Boot-menu keys

The device tree puts the keys on TLMM GPIOs (`gpio_keys`: VOL+ 101, SHOOT 95, FOCUS 179, flags `1` =
active low). Three driver variants were tried from DXE, and every one that touches the TLMM window
makes the firmware assert immediately after `Nx741jKeysDxe` is loaded, ending in ZTE memory-dump mode:

| Variant | Result |
|---|---|
| GCD descriptor check + `AddMemorySpace` + pin config writes | assert |
| `AddMemorySpace` + pin config writes | assert |
| plain read of `GPIO_IN_OUT` | assert |

`Nx741jKeysDxe` therefore publishes its `SimpleTextInputEx` device with the TLMM path disabled: the
menu renders and the `Connect ConIn failed` message is gone, but keys are inert. The documented
OS-side path (`ShengHidInput` `ButtonProbe`, `buttons=1`, report 2 bits for Power / Volume) is the
next step; the PMIC keypad (`KeypadDxe` + a ZTE `KeypadDeviceLib`) is the firmware fallback.

### Real partition layout (vendor GPT backup, 4096-byte logical blocks)

| LUN | Partition | First LBA | Last LBA | Size |
|---|---|---|---|---|
| 0 | `persist` | 8 | 8199 | 32 MiB |
| 0 | `misc` | 8200 | 8455 | 1 MiB |
| 0 | `metadata` | 4727592 | 4743975 | 64 MiB |
| 0 | `rawdump` | 4743976 | 4875047 | 512 MiB |
| 0 | `userdata` | 4875048 | 4875047 | 0 in this backup |
| 4 | `boot_a` | 119078 | 143653 | 96 MiB |
| 4 | `efisp` | 748894 | 749661 | 3 MiB |

`rawdump` is empty in the vendor image, which is why the payload (offset 0) and the firmware FD
(+16384 sectors) can live there without touching Android. The `userdata` entry in this backup has zero
length, so the live GPT has to be read again before stage 5 uses it.

### UFS facts from the shipped device tree (`dts/nx741j-adt.dts`)

| Property | Value | Used by |
|---|---|---|
| `reg` `ufs_mem` | `0x1d84000` / `0x3000` | `NativeUfs` host window |
| `reg` `ice` | `0x1d88000` / `0x18000` | inline crypto engine |
| `reg` `mcq_sqd` / `mcq_vs` | `0x1da5000`/`0x2000`, `0x1da4000`/`0x10` | MCQ (unused; MCA is derived at `0x1da0000`) |
| PHY | `ufsphy_mem@1d80000`, `qcom,ufs-phy-qmp-v4-canoe` | `package/storage-bringup/ufs-cold-init.c` (`0x1d80000`) |
| `iommus` | `apps_smmu 0x60` | bypass in `SmmuTakeover.h` uses stream `0x60` |
| `interrupts` | SPI 265, level high | not wired; the driver polls |
| `dma-coherent`, `qcom,iommu-dma = "fastmap"` | present | queues may be cached; identity IOVA |
| `power-domains` | host `<gcc 3>`, PHY `<gcc 2>` | not enabled explicitly |
| `reset-gpios` | TLMM `0xd9` (217), active low | not used |
| `lanes-per-direction` | 2 | — |
## Building

Two entry points, both run on Linux/WSL:

```sh
# Fast iteration: kernel -> payload -> firmware, no QEMU smoke.
./scripts/build-fast.sh ~/nx741j-build/UniXNU

# Milestone gate: the same three steps plus a QEMU boot-chain run.
# Use it before flashing anything that touches the firmware memory layout.
sudo ./scripts/build-and-test.sh ~/nx741j-build/UniXNU
```

The gate asserts three markers — `UNIXNU: BootEntry`, `MARK #29`, `QEMU: EBS ok, entering the
kernel` — and fails the build when any is missing.

## Flashing

The vendor ABL on this device rejects `fastboot boot`, `fastboot flash` and `fastboot erase`
(`unknown command`), so everything goes through **9008** and the EFI system partition:

| Region | LUN | LBA | Size |
|---|---|---|---|
| `efisp` (our loader) | 4 | 748894 | 3 MiB |
| `rawdump` — firmware FD | 0 | 4760360 | 4 MiB |
| `rawdump` — payload | 0 | 4743976 | 64 MiB |

```powershell
wsl\flash-boota-9008.ps1 -Img <image> -StartSector <lba> -Label <label> -Lun <lun> -PartitionBytes <bytes>

# read back instead of writing (log channel, register dumps, verification)
wsl\flash-boota-9008.ps1 -Img <any> -ReadTo _tmp\dump.bin -StartSector <lba> -ReadSectors <n> `
    -Label rawdump -Lun 0
```

Any Android boot overwrites `efisp`, so it has to be re-flashed before every payload test. Writing
back the stock `efisp` (`sha256 4df18359...`) restores the vendor boot chain.

## Repository layout

| Path | Contents |
|---|---|
| `board.toml` | board description: components, kernel archives, prelink roots, boot args |
| `dts/` | ADT source for the payload |
| `loader/` | efisp EFI loader (`p5.c` ... `p7.c`) and its images |
| `package/firmware-overlay/` | board-level firmware code, applied into the UEFI tree |
| `patches/xnu/` | device XNU patch series (see `series` for the applied order) |
| `patches/firmware/` | firmware patches (DSC/FDF, platform drivers) |
| `scripts/` | build helpers (`build-fast.sh`, `build-and-test.sh`, ...) |
| `docs/` | analysis, audits, per-stage notes, hardware inventory |

## Recent fixes worth knowing

| Patch / change | Why |
|---|---|
| `0010-console-acquire-screen.patch` | `PE_create_console` uses `kPEAcquireScreen`, so `kprintf` reaches the panel |
| `0015-iodevicetree-null-interrupt-parent.patch` | `IODTMapOneInterrupt` fed a NULL parent into `IODTGetICellCounts` and panicked during `InitIOKit` |
| `component:GopFramebuffer` in `[kernel] archives` | the display component was selected but never linked |
| `native-ufs-cold-init=1` | vendor-derived PHY calibration + link startup before any command |
| `patches/xnu/0021-console-log-capture.patch` | console bytes are mirrored into a DRAM buffer the efisp loader can find on the next boot |
| `scripts/patch-native-ufs-adopt-fix.py` | the adopt path cleaned 32 KiB out of a 16 KiB mapping, which faulted in `adoptAndRead+0x844` |
| `patch-native-ufs-cold-init.py` (unconditional) | the cold-init hook was only called for `probe!=0`, so it never ran with the takeover settings |
| `wsl/flash-boota-9008.ps1 -ReadTo` | 9008 read-back, used for the log channel and for register dumps |

## Documentation

- [docs/TASKS.md](docs/TASKS.md) — consolidated task and status log
- [docs/STATUS-2026-11-11.md](docs/STATUS-2026-11-11.md) — stage-by-stage status
- [docs/BOOT-CHAIN.md](docs/BOOT-CHAIN.md) — boot chain against the official description
- [docs/UFS-QCOM-PHY-PORT.md](docs/UFS-QCOM-PHY-PORT.md) — restored PHY tables and call order
- [docs/HARDWARE-INVENTORY.md](docs/HARDWARE-INVENTORY.md) — hardware facts with sources

## Licence

Patches and board code follow the upstream UniXNU licensing (BSD-2-Clause-Patent where noted in the
individual files). Vendor-derived tables are data recovered from the device's own firmware.

## Tasks, TODOs and open issues (2026-11-18)

Everything below is measured on the unit; the evidence for each line lives in
[docs/STATUS-2026-11-18.md](docs/STATUS-2026-11-18.md) and the earlier status files.

### Done

| Item | Evidence |
|---|---|
| stages 1-5: board, firmware, payload, hand-offs, kernel bring-up | [docs/STATUS-2026-11-12.md](docs/STATUS-2026-11-12.md) |
| console log channel (kernel -> DRAM -> rawdump -> host) | `patches/xnu/0021`, `loader/p7.c`, `-ReadTo` |
| UFS register access, queue ownership, UTRLBA/RSR readback | [docs/STATUS-2026-11-15.md](docs/STATUS-2026-11-15.md) |
| stream 0x60 match + bypass from the kernel (SMR[0]=0x80000060, S2CR[0]=1) | 2026-11-18 section 1 |
| DME_GET / DME_SET (PA_PWRMode SLOW -> FAST) | 2026-11-18 section 2 |
| sweet_ufs port and its libkern-only build | `patches/xnu/0022` |
| standalone loader port + Mach-O to SWEETXNU payload converter | `bootshim/`, `scripts/macho-to-sweet-payload.py` |

### Blocked

**Stage 6 - UFS cannot be driven from HLOS on this unit.** The host never fetches the
UTRD (DBR stays set), both from our DMA and from the bootloader's own list region,
while UTRLRSR reads 1 and HCS=0x10f. Every candidate cause was measured and excluded:

* firmware: removing UfsPassThruDxe changes nothing (section 9);
* MCQ-only controller: CAP bit29 = 0, legacy doorbell is supported (section 4);
* descriptor encoding: matches `ufshci.h` bit for bit (section 4);
* controller reset: the HCE cycle leaves the host unresponsive (section 3);
* SMMU context bank: refused to XNU, to the firmware and through TZ_IO_ACCESS_READ (sections 5, 7);
* the bootloader never handed SMR0 over: the DT declares `qcom,handoff-smrs = <0x800 0x02>`
  (SMR0 set up by the bootloader, as on the working data-a-live unit) but the measured
  value is `SMR[0]=0x00000060` with VALID clear and `S2CR[0]` faulting;
* bootloader configuration: `xbl_config.img` is a signed MBN whose UFS keys are a schema
  table only, and `NX741_unlock/` contains no occurrence of the key (section 10).

**Stage 7 - userland** stays blocked by stage 6.

### Next steps (each needs something we do not have)

1. **Vendor-signed XBL/TZ change** so stream 0x60 stays matched and bypassed for the
   non-secure world, or so a stage-1 context the kernel may program exists.
2. **Firmware-side root mount**: Mu would have to service block requests after
   ExitBootServices. Mu is single-core (no MpServices/PSCI in Mu_Basecore), so this
   needs an SMP port, a shared-ring protocol and an XNU block driver.
3. **Standalone EFI loader**: `fastboot boot` on this unit requires a PE in the kernel field
   (`No EFI image found in boot.img`), so the loader must become a PE and the payload
   must ride in the ramdisk field (section 11).

### Backlog

| Task | Notes |
|---|---|
| ADT: `unixnu,adt-name` on peripheral nodes | stage 7 needs matching |
| ADT: `/chosen` osenvironment, crypto-hash-method, root-matching | missing today |
| ADT: `memory-map` placeholder | currently empty |
| panic text into the log channel | `panic()` output is not captured, only the panel shows it |
| keys from the OS side (ButtonProbe, report 2) | any DXE access to TLMM asserts |
| `0008-granular-protection-noop.patch` not in `series` | `board.toml` comment still references it |
| record the `early_security_init = ""` (AMFI/Image4) deviation | needed before stage 7 |
| trust cache / unique_chip_id placeholders | self-made flat package; document before release |
| bootshim build output in git | excluded via `.gitignore` since 719722b |

### Open questions

* Which stage clears SMR[0].VALID - XBL, ABL or TZ? The read-only walk runs after the
  firmware, so the writer is not identified yet.
* What is the compiled value of UfsSmmuConfigForOtherBootDev in `xbl_config`?
* Would TZ accept a stage-1 context built by HLOS if the context-bank window were opened?

### Reproducing the evidence

```
wsl bash _tmp/t2/noaes2_2017.sh      # kernel with the probe patches
wsl bash _tmp/t2/pay_step2b_2017.sh  # payload (boot-args from board.toml)
wsl bash _tmp/t2/qemu_step2_2017.sh  # gate: EBS ok
wsl bash _tmp/t2/fw_2017.sh          # firmware (SILICIUM_UEFI.fd)
```

On the unit: flash over 9008 (`wsl/flash-boota-9008.ps1`), boot once, warm reset,
re-enter 9008, read the log with `-ReadTo`.

## 2026-11-19 update — UFS commands complete, data phase next

### Done since the last snapshot

| Item | Evidence |
|---|---|
| cold init completes the vendor sequence (PHY, SERDES, PCS, HCE, DME_LINKSTARTUP) | `PCS ready (0x400 00000001)`, `HCS=0x10f`, `UPMCRS=1` |
| UFS GCC branches are all enabled (SM8850 offsets from data-a-live) | `axi/ahb/unipro/aggre/ice` enable bits set |
| first completed UFS command on this unit | `TEST UNIT READY lun0: ocs=0 resp=00 scsi=00 db=0` |
| the host only fetches from the firmware's queue region | own DMA (`0xb4a09000`) -> `dbr=1`; firmware region -> command completes |
| waits are bounded (no more timer stalls) | `IODelay` replaced by busy-waits at six sites, per the official UFS page |

### Open issues (in order)

1. **Data phase writes nothing we can see.** `READ CAPACITY` completes but the 8 bytes are
   zeros (`unexpected LUN0 block size 00000000`). The adopt path cleaned the firmware
   mapping by virtual address; it now flushes by physical address like data-a-live does,
   and probe 9 scans the 16 KiB region for the expected pattern before panicking. Result pending.
2. **Kernel window excludes the firmware queue region.** `arm_vm_init` rejected
   `0xc3b04000` because DarwinBoot handed over a 256 MiB window (`0xb03c8000 + 0x10000000`).
   `WINDOW_SIZE` is now 512 MiB and the firmware is rebuilt; the next boot tells whether the
   allocation lands so that the queue region is inside it.
3. **Stage 7 cannot start** until a known block is read reliably (issue 1) and the root volume
   is located.
4. **ADT gaps** before userland: `unixnu,adt-name` on peripherals, `/chosen`
   `osenvironment`/`crypto-hash-method`/`root-matching`, `memory-map` placeholder.
5. **panic text is not in the log channel**; it is read off the panel only.
6. **Vendor side**: the DT declares `qcom,handoff-smrs = <0x800 0x02>` but the bootloader does
   not perform it; the switch lives in a signed `xbl_config` MBN, so only a vendor-signed
   XBL/TZ change can alter it. This is why the port cannot rely on HLOS owning the SMMU context.

### Reproducing

```
wsl bash _tmp/t2/noaes2_2017.sh      # kernel (patch-native-ufs-cold-init.py applies the probes)
wsl bash _tmp/t2/pay_step2b_2017.sh  # payload (boot-args from board.toml)
wsl bash _tmp/t2/qemu_step2_2017.sh  # gate: EBS ok
wsl bash _tmp/t2/fw_2017.sh          # firmware
```
