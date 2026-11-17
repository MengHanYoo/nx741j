# Design — UFS DMA through apps_smmu (IOVA 0), per this board's ADT

## Evidence (this machine's own device tree)

```
ufshc@1d84000 {
    reg           = <0x0 0x1d84000 0x0 0x3000   /* host   */
                     0x0 0x1d88000 0x0 0x18000  /* phy    */
                     0x0 0x1da5000 0x0 0x2000   /* ice?   */
                     0x0 0x1da4000 0x0 0x10>;   /* crypto */
    iommus        = <&apps_smmu 0x60 0x0>;       /* stream 0x60 */
    memory-region = <&ufshc_dma_resv_region>;
};

ufshc_dma_resv_region {
    iommu-addresses = <&ufshc 0x0 0x0 0x0 0x1000>;   /* IOVA 0, 4 KiB */
};
```

This is the standard Qualcomm contract: the UFS host's request list lives at
**IOVA 0x0** inside a 4 KiB reserved window, and *all* UFS DMA is expressed in
IOVA space, translated by `apps_smmu` stream `0x60`. Linux implements exactly
this (`ufshcd-qcom` reserved region + `iommu-addresses = <&ufshc 0 0 0 0x1000>`,
then `UTRLBA = 0`).

## Why everything we tried stalled

Our driver programs `UTRLBA` with a **physical** address while the SMMU is off
(`SCR0 bit0 = 0`) and while the stream/context the platform expects was never
built. With this board's contract, the host issues descriptor fetches into the
UFS IOVA space; with no translation and no mapping at IOVA 0 the fetch has no
owner and the interconnect stalls — which is exactly the symptom: every register
access works, a store of 0 to `UTRLDBR` returns, and any store that sets a slot
hangs the bus.

What is excluded by measurement (see STATUS-2026-11-15/17):

| Hypothesis | Evidence |
|---|---|
| firmware changed controller state | ABL snapshot identical (HCS/HCE/RSR/MEM_CFG) |
| Mu left bad state | DarwinBoot quiesce (HCE cycle) ran, stall unchanged |
| SMMU globally bypassed is enough | `SCR0 = 0x00270406`, i.e. disabled — not a working bypass for this contract |
| MCQ | `MEM_CFG = 0` |
| clocks | AHB/UNIPRO forced on, no change |
| queue ownership | `UTRLBA` readback matches, `RSR = 1` |
| region choice | both our DMA and the ABL region stall |

## Design

1. **Tables.** Build a 3-level native-4K page table for a 32-bit IOVA space inside
   our existing 80 KiB DMA block (offsets `0x10000..0x13fff` are free): L1, L2
   and one L3 page. Use the `ArmSmmu4K` kit's live arena (`ArmSmmu4KLiveArena`
   with `ArmSmmu4KTablePages`) so the tables are the verified ones and the kit
   computes the TCR/leaf encodings instead of hand-rolled constants.
2. **Mappings.** Identity in IOVA space over our DMA block:
   `IOVA 0x0000..0x13fff -> PA(dma_pa)..` with normal memory attributes.
   The request list is at IOVA 0 (offset 0 of the block), UCD at IOVA 0x1000,
   bounce at IOVA 0x4000 — the existing layout works unchanged.
3. **Stream and bank.** `SMR[0x60] = 0` (valid, mask 0), `S2CR[0x60] = 0`
   (translation through CB0), CBAR[0] = CB0 base (type 0), then program CB0
   through the kit (`ArmSmmu4KProgramDisabledContext`) and only then set
   `SCR0 |= 1`.
4. **Host programming.** `UTRLBA = 0`, `UTRLBAU = 0`, `UCDBA = 0x1000`;
   PRDT entries point at IOVA 0x4000+; ring as today.
5. **Failure handling.** If any step fails, leave `SCR0` disabled and report
   through the existing panic probe `NX741J UFS: …` so the screen shows which
   stage failed.

## Where

`drivers/ShengUfs/NativeUfs.cpp` (C++: the kit is C-linkage, the component is
already built into the kernel). The BSD `sweet_ufs.c` probe stays as the
falsification harness; the IOVA path is a separate probe value
(`native-ufs-probe=30`) so both can be compared on device.

## Verification

1. QEMU gate unchanged (the probe only runs on hardware).
2. On device: `native-ufs-probe=30` → panic reports `smmu scr0/smr/s2cr`,
   `UTRLBA readback`, `DBR`, `IS`, `OCS`. Success is `dbr=0` and
   `ocs=0`, then GPT lookup and a known-block read.
3. Only then wire the production path (root device) as in the guide's checks 3–7.

## Risks

* Wrong TCR/leaf attributes fault instead of stalling — the panic probe reports
  `IS`/fault registers so a fault is distinguishable from the old stall.
* Enabling `SCR0` affects every stream: the kit's programming step is per-bank
  and refuses enabled banks, and XNU's own ArmSmmu4K driver must not own the
  same bank. Probe runs before any other SMMU user on this board.
