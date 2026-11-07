# NX741J / UniXNU archive - 2026-11-08 (stage 2 complete)

Device: Nubia Z80 Ultra (NX741J / SM8850 canoe), iOS 16.7.16 (20H392).

## Status
- Stage 1 (efisp loader -> Mu firmware): done
- Stage 2 (UEFI -> UniXNU menu + payload volume + unixnu.toml): done, NX741J entry visible
- Stage 3 (DarwinBoot -> XNU): failing, black screen then MemoryDump when booting xnu
- UEFI physical keys: not working (TLMM MMIO window not mapped, vendor protocol chain absent)

## Files (SHA-256)

- firmware/fd-v15-min.img : bd5cd929d9619d0bab6734ee9e51ad351f492a2c6532891516f526de24d7fd53 : 4194304 bytes
- firmware/fd-v23-payload-menu.img : bc1318c0e0e055f5937855e4a2c6a313ff0f1f85a0ff03d2233d603a1a5354b7 : 4194304 bytes
- firmware/fd-v33-no-uc-rng.img : 9910645d006e5fba31a839008f9afab310145b14cf1446b0134e1cecaa3cc652 : 4194304 bytes
- firmware/fd-v34-keys-mapped.img : d7dbfb2e7886481a9c131b26a3c7f9361eaf77e44dfbaad8cd780d61d0143bc8 : 4194304 bytes
- firmware/fd-v35-keys-release.img : 6550e590f9d3b15bd8975dafdbd200f1849d4da73c69d08c04e8e32b196a4d30 : 4194304 bytes
- loader/efisp-p5.img : 1e8bdf046c45b7c12cee0e1838abb5c9f70c7a8df0e9a02088774e474355fe29 : 3145728 bytes
- loader/efisp-p9.img : 2eeb6bfe2ddf0d8e94db4e9195119ab097739e1777dbee6a705bfeb9b011a182 : 3145728 bytes
- loader/efisp-stock-rollback.img : 4df183594a63af28c681e48b1d9fdb0305ee055ea8fde92118a1e0af7a5f6f17 : 3145728 bytes
- loader/p5.c : 313e2cfc1077941e2067ee2337054ad726d70c9f5a70f41e6669acae648522bc : 15951 bytes
- loader/p9.c : 0ed58db7bdacc9ecb987bb3407ffc5cc0eb8b9cce83cfe3ed168f78f48b1bf3a : 16915 bytes
- payload/payload-panic.img : eb5e29e38d001649794bd97d51c54fc153591ed532c56235f8440d06c35c55e6 : 67108864 bytes
- docs/ANALYSIS-EFISP-VENDORBOOT.md : 609ca604a56e38e1e6f0da5478550adfb45fdb62a3cd3b4ae82d36772cd10024 : 54164 bytes
- docs/BOOT-CHAIN.md : e4a4addd3b28c1bdea257c31f34a0959750d1b43991afbdcdbaacfd5b570103c : 6463 bytes
- docs/CONFIG-MAP.md : c3c37ad9fd453adca42263b097b4077bd4a2cdbcb8f4d0604378030f8e856482 : 32983 bytes
- docs/DRIVER-CHECKLIST.md : 9551cb9115004fae8a63b6f6556b28d482a44bf357ea1175b649a8de0917caef : 6496 bytes
- docs/FIRMWARE-DRIVERS.md : c986caf3502dbf4891403d4b7507fb9c3cb795d11c0fcff7e0b7eda009e06056 : 4321 bytes
- docs/GLOSSARY-LOCAL.md : a97c2995a9ca54cb240b8e5222982b8aad643c431b22e819ec7bec085aadf04d : 3110 bytes
- docs/HANDOFF-2026-11-06.md : eaf26f616e9a721b9f0890b9f1ba2dea161aa6a726507a8ffe2b547564772ba1 : 12667 bytes
- docs/HANDOFF-2026-11-07.md : 4eaa19c24ba836e46c61f7603d67eb7df97406f85daa4fd94f77fc4207ddd545 : 23611 bytes
- docs/HARDWARE-INVENTORY.md : 797014a5c84e614b094b82091fb23a5327f78aa4238c69b85b1545a027678552 : 6734 bytes
- docs/PORTING-RECORD.md : 06d9b62fdf21f4d414fbb5af68696006d5946ff5d8e6b7806bdf8b2402549983 : 57115 bytes
- docs/SHARED-SUPPORT-SELECTION.md : f7da32f44fbf2b12fb4879a931dcf54eb3ebb8db42152b904398719fc9499510 : 4303 bytes
- docs/SHARED-SUPPORT.md : 80a08709c43569dea0f735b9b1b945f8797df95bc0f89e43888e966d67e8adce : 5903 bytes
- docs/STAGE2-CONFORMANCE.md : 5fa581b6c8a6c3c65b6aeffd9554270aedbf35451a207baac3cd8f4b6b9610cd : 6572 bytes
- docs/START-HERE.md : da121f7752809e32a071b8ea4a8bb30fa6f9630bfec7f42a14ae4121bcb012c2 : 11520 bytes
- docs/TASKS.md : d8f323d8cafdd9b117ac759370e9ad7552779f9cac9e438342334aff4c99b36d : 5107 bytes

## Flashing (9008)
- firmware FD -> rawdump LBA 4760360 (LUN0), 1024 sectors, 4 MiB
- payload     -> rawdump LBA 4743976 (LUN0), 16384 sectors, 64 MiB
- loader      -> efisp   LBA 748894 (LUN4), 768 sectors, 3 MiB
- Android rollback: write loader/efisp-stock-rollback.img to efisp

## Repositories
- device: MengHanYoo/nx741j branch unixnu-nx741j, tag stage2-2026-11-08
- framework: MengHanYooo? see remote; branch unixnu-main
