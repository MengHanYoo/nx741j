# NX741J proven iOS route — 2026-11-23

> This document supersedes the Mu/UEFI diagnostic route as the device bring-up
> path. The source tree keeps the UEFI work for later cleanup, but the image
> installed on the phone uses the locally proven shim-loader route.

## Final route

```text
XBL/ABL
  -> stock efisp LinuxLoader
  -> boot_a: Android v4 header + NX741J loader Image
  -> embedded SWEETXNU payload
  -> XNU 8796.141.3
  -> HFSX ufsroot0
  -> launchd
```

This is the route that previously reached the iOS lockscreen, userspace,
display, touch and Metal work on this unit. It replaces:

```text
efisp P7 -> Mu BDS -> UniXNUDxe -> UniXNU.efi -> DarwinBoot -> XNU
```

The latter stopped at Mu handle enumeration (`SmemStubDxe: no handles to
connect`, `msbootpolicy.efi`, `Image not found`) and is not the final device
route.

## Inputs

| Input | Result |
|---|---|
| Base root | original iPhone X 20H392 IPSW build, SHA-256 `7fd949bb026ef84d22bf316f28c41234358e41c65e4f393e1a3d1ffe1731b6ea` |
| Patched root | `iosroot-NX741J-20H392-proven-20261122.img`, SHA-256 `dd7dee484eee2faf03484f5c9726682ed562fe7163dd3a362d167454df280ac4` |
| Pinned extent table | 39 extents, 4,194,304 blocks, DMA below 36 bits |
| Live GPT | LUN0 `userdata` first `4,875,048`, last `248,838,138`, 4 KiB LBAs |
| Proven kernel | `/opt/work/plk/k39-amfi.macho`, SHA-256 `f28d3db2316269a9c6e4d2fa75e00ed598bf0aa419e97a87141e7e11407c86ba` |
| Payload | SHA-256 `6c0332d186a66f3efbcfe96f9d751248c114533a1ba451d459fe74cc65ea6dde` |
| ADT | SHA-256 `bfa844a8a2dfc806009a8de70083ae1872dd18a4916e9e49cecd5c3112e3ab63` |
| Loader Image | SHA-256 `459fe5ccfa83a08540b326f7198e8e28fa820b52698def50a509fe76fece3c97` |
| `boot_a` image | SHA-256 `2523e380b0500bb3e65d012a0626876d4db20b5cc38278ee01c70c05c947cef0` |
| Stock efisp | SHA-256 `4df183594a63af28c681e48b1d9fdb0305ee055ea8fde92118a1e0af7a5f6f17` |

The patched root was derived from the original IPSW root; `iosroot-GOLDEN.img`
was not used as a build input.

## Root fixes

The plain data-a-live-style root reached launchd but lacked NX741J-specific
container state. `scripts/apply-nx741j-root-bringup.py` adds:

- `systemgroup.com.apple.mobilegestaltcache` with the required container
  metadata and an empty version-1 `com.apple.MobileGestalt.plist`;
- the 12-entry launchd disabled table for baseband, Bluetooth, FairPlay,
  DriverKit, BootPD, DHCPv6, relevance and Siri test jobs;
- SpringBoard `SBDontDimOrLockOnAC`, `SBDisableIdleSleep`,
  `SBDisableAutoDim` and `BKNoWatchdogs`.

The complete patched image differs from the verified base image in only
13 blocks. They map to nine physically contiguous ranges in the pinned file:

| Item | Value |
|---|---|
| changed bytes | 53,248 |
| changed 4 KiB blocks | 13 |
| physical write ranges | 9 |
| userdata sectors | 5,743,400 through 9,516,625 |

`scripts/diff-pinned-root.py` maps logical blocks through the 39-extent table;
`scripts/extract-root-delta.py` emits and hashes the exact write files.

## Boot payload

The proven source path is:

```text
k39-amfi.macho
  + mkadt_nx741j.py --root-extents <current 39-extent JSON>
  + two original trust-cache modules
  -> mkpayload.py
  -> SWEETXNU v1, 16 segments
  -> WSL POSIX loader build
  -> Android boot header v4, kernel-only, no ramdisk
```

The ADT deliberately does not contain stale `pandora-log-extents` or
`pandora-fw-extents`. Those old mappings belonged to the previous root and
could have written through reused userdata blocks.

Boot arguments are the proven first-lockscreen set:

```text
debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress
rd=ufsroot0 ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1
wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1
sweetfb_hz=30 sweetfb_fill=1 pandora_scanout=0
sweetsample=MobileGestaltHel,containermanage,cfprefsd,lsd,SpringBoard
```

## Validation

Static gate:

```sh
scripts/check-proven-route.py \
  --boot <loader-boot.img> \
  --root <proven-root.img> \
  --extents <pinfile-extents.json> \
  --adt-tools <nx741j/tools>
```

It verifies the Android v4 header, raw arm64 Image, zero ramdisk,
`SWEETXNU`, all 16 segments, ADT root mapping, absence of stale physical
mappings, the two trust caches, boot arguments and the 16 GiB root hash.

QEMU runs the same loader and XNU image without the Mu/DarwinBoot smoke path.
The kernel reaches interrupt-controller setup and stops at:

```text
SWEET GIC: virtual timer INTID 27
panic: find_gicr_pe_base: cannot find GICR base for core 0 @pe_fiq.c:89
```

That is expected for QEMU `virt`: the ADT correctly contains the phone's
`GICR@0x17080000`, which QEMU does not model. The result proves loader,
payload, ADT, trust cache and XNU entry are intact; it does not model NX741J
UFS, GIC, display, touch or PMGR.

## Real-device write

`scripts/host/write-proven-route-9008.ps1` performed one Firehose session:

1. read the live 4K GPT and require the pinned `userdata` bounds;
2. read all nine changed root ranges and require their original hashes;
3. write the 13 changed root blocks;
4. restore stock efisp;
5. write `boot_a`;
6. read every region back and compare;
7. read the GPT again and require it unchanged;
8. reset.

The write report is:

```text
out/proven-route/device-write-20261123-000200/write-report.json
```

All nine root ranges, stock efisp and `boot_a` matched on readback. The live
GPT remained `4,875,048..248,838,138`.

After reset, Windows produced no `9008`, `QUSB_BULK`, Android or ADB device
for at least 45 seconds. This removes the previous persistent
`QUSB_BULK_CID:046A_SN:098F0FC3` failure signature and is consistent with the
known iOS state where USB remains unavailable because step 7 is frozen.
Direct launchd/lockscreen evidence still has to be captured by the next
available userspace or display-side log channel; absence of QUSB is not
treated as proof of launchd in this report.

## Progress

| Official stage | NX741J state |
|---|---|
| 1 inputs and build tree | complete |
| 2 shared support and OS ABI | complete |
| 3 device repository and firmware data | complete |
| 4 build pipeline | complete |
| 5 hardware description / ADT | complete |
| 6 drivers | storage, display, touch, HID and CPU paths are in the proven kernel; persistent Data, sustained performance and final thermal policy remain |
| 7 USB/Wi-Fi/audio/baseband/camera/back display | frozen by owner |
| 8 records, startup and recovery | install/QEMU/readback records complete; direct launchd capture remains the one observability gap |

Current overall engineering coverage is approximately **90%** for booting the
existing iOS 16.7.16 root. The step-7 subsystem work is intentionally excluded
from that number. The former 80% figure described the unfinished
Mu/NativeUfs/APFS route and no longer applies.
