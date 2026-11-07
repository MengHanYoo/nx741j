# Firmware overlay for the efisp loader work (2026-11-07)

`Binaries/` is a git submodule (`Project-Silicium/Device-Binaries`) and is ignored by the
parent repository, so the `AndroidDxe` binary cannot travel inside a diff. Apply it first:

```sh
cp -r overlay/Binaries/nx741j/QcomPkg/Drivers/AndroidDxe \
      <uefi-tree>/Binaries/nx741j/QcomPkg/Drivers/
```

Then apply the platform patch from the device repository root:

```sh
cd <uefi-tree>
git apply <device-repo>/patches/firmware/2026-11-07-efisp-loader-platform.patch
```

The patch touches `Platforms/ZTE/nx741jPkg`:

| File | Change |
|---|---|
| `Drivers/LogDumpDxe/{LogDumpDxe.c,LogDumpDxe.inf}` | new: frame-buffer capture to `rawdump` (LocateProtocol based) |
| `Drivers/SmemStubDxe/{SmemStubDxe.c,SmemStubDxe.inf}` | capture code + `ConnectController` sweep |
| `Include/DXE.inc`, `Include/APRIORI.inc` | `TzDxeLA` re-enabled, `AndroidDxe` added before `HALIOMMU` |
| `nx741j.dsc`, `nx741j.fdf` | `LogDumpDxe` and `AndroidDxe` wired in |

Resulting firmware volume: build with `./unixnu build firmware -D <device>`, then write the
FD to `rawdump` + 64 MiB and the loader image to `efisp` (see `docs/ANALYSIS-EFISP-VENDORBOOT.md`).
