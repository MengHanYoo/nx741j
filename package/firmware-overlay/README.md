# nx741j firmware overlay

`scripts/apply-firmware-overlay.py <uefi-tree>` copies this tree over the Mu-Silicium
checkout before `./unixnu build firmware`. Paths mirror the Mu-Silicium layout, so a file
here replaces the upstream one:

- `Platforms/ZTE/nx741jPkg/` — DSC/FDF, the include lists, and the board drivers
  (`LogDumpDxe`, `SmemStubDxe`, `StopDxe`, `Nx741jKeysDxe`, `TlmmPei`, `TlmmProbeDxe`).
- `Silicon/Qualcomm/.../RfsProtection.c` — the SCM-missing notice is verbose (BDS keeps running).
- `Silicon/Silicium/.../KeyCallback.c`, `.../Sec.c` — board boot-manager and SEC changes.

The important integration (reference/uefi-firmware): `DarwinBoot.inf` is dispatched through
`Include/APRIORI.inc` before `BdsDxe`, because UniXNU looks up the loader protocol when the
menu boots the `xnu` entry. `nx741j.fdf` no longer repeats it (the APRIORI list adds it to
the volume) and drops the non-essential `RamManagerDxe`, `SmbiosDxe`, `SmBiosTableDxe` and
`IortDtDxe` that only logged missing vendor protocols.
