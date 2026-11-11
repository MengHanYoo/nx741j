/*
 * ufs-qcom-phy.c - QMP UFS v4 "canoe" PHY tables and sequences (SM8850 / NX741J).
 *
 * Reconstructed from the vendor modules shipped for this device:
 *   phy-qcom-ufs-qmp-v4-canoe.ko  - the four calibration tables, start_serdes,
 *                                   is_pcs_ready (PCS register 0x400 bit 0)
 *   phy-qcom-ufs.ko               - ufs_qcom_phy_write_tbl(): 8-byte {reg, val}
 *                                   entries written with writel to phy->mmio
 *
 * The tables are byte-for-byte dumps of the module .data, not reconstructions.
 * They are kept freestanding (volatile u32 pointer + no kernel headers) so the
 * same file can be folded into ShengUfs or exercised from a host test.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ufs_phy_reg {
	uint32_t reg;
	uint32_t val;
};

/* HS-G5 rate A: the full PCS/QSERDES programming, 106 writes. */
static const struct ufs_phy_reg phy_cal_table_rate_A_g5[] = {
	{ 0x0404, 0x1 },
	{ 0x05fc, 0xa },
	{ 0x0110, 0xd9 },
	{ 0x0174, 0x16 },
	{ 0x003c, 0x11 },
	{ 0x009c, 0x0 },
	{ 0x0120, 0x1 },
	{ 0x0124, 0x60 },
	{ 0x00f4, 0x1f },
	{ 0x00f8, 0x1f },
	{ 0x00fc, 0x7 },
	{ 0x0100, 0x20 },
	{ 0x0140, 0x4 },
	{ 0x013c, 0x40 },
	{ 0x0268, 0x6 },
	{ 0x0088, 0x41 },
	{ 0x0070, 0x6 },
	{ 0x0074, 0x18 },
	{ 0x0078, 0x14 },
	{ 0x026c, 0x6 },
	{ 0x0270, 0x18 },
	{ 0x0274, 0x14 },
	{ 0x0080, 0x7f },
	{ 0x0084, 0x6 },
	{ 0x0058, 0x92 },
	{ 0x005c, 0x1e },
	{ 0x0028, 0x4c },
	{ 0x0010, 0x6 },
	{ 0x0014, 0x18 },
	{ 0x0018, 0x14 },
	{ 0x0278, 0x6 },
	{ 0x027c, 0x18 },
	{ 0x0280, 0x14 },
	{ 0x0020, 0x99 },
	{ 0x0024, 0x7 },
	{ 0x0050, 0xbe },
	{ 0x0054, 0x23 },
	{ 0x107c, 0x0 },
	{ 0x1030, 0x7 },
	{ 0x1034, 0x17 },
	{ 0x12d4, 0xc },
	{ 0x12dc, 0xc },
	{ 0x12f0, 0x4 },
	{ 0x13cc, 0x14 },
	{ 0x12f4, 0x7 },
	{ 0x13d4, 0xe },
	{ 0x1254, 0x2 },
	{ 0x1210, 0x1c },
	{ 0x1224, 0x6 },
	{ 0x1378, 0x8e },
	{ 0x13b4, 0xf },
	{ 0x1418, 0xce },
	{ 0x141c, 0xce },
	{ 0x1420, 0x18 },
	{ 0x1424, 0x1a },
	{ 0x1428, 0xf },
	{ 0x1430, 0x60 },
	{ 0x1434, 0x62 },
	{ 0x1448, 0x9a },
	{ 0x1454, 0xe2 },
	{ 0x1458, 0x6 },
	{ 0x1460, 0x1b },
	{ 0x1464, 0x1b },
	{ 0x1468, 0x98 },
	{ 0x146c, 0x9b },
	{ 0x1470, 0x2a },
	{ 0x1474, 0x12 },
	{ 0x147c, 0x6 },
	{ 0x1480, 0x1 },
	{ 0x1484, 0x93 },
	{ 0x1488, 0x93 },
	{ 0x148c, 0x60 },
	{ 0x1490, 0x99 },
	{ 0x1494, 0x5f },
	{ 0x1498, 0x92 },
	{ 0x149c, 0xe3 },
	{ 0x14a0, 0x6 },
	{ 0x14a8, 0x9b },
	{ 0x14ac, 0x9b },
	{ 0x14b0, 0x60 },
	{ 0x14b4, 0x99 },
	{ 0x14b8, 0x5f },
	{ 0x14bc, 0x92 },
	{ 0x14c0, 0xfb },
	{ 0x14c4, 0x6 },
	{ 0x1228, 0x1f },
	{ 0x1258, 0x94 },
	{ 0x12c4, 0xfa },
	{ 0x1548, 0x30 },
	{ 0x1580, 0x77 },
	{ 0x05f4, 0x43 },
	{ 0x0420, 0x40 },
	{ 0x042c, 0x33 },
	{ 0x0474, 0x5 },
	{ 0x04bc, 0x5 },
	{ 0x0430, 0xf },
	{ 0x058c, 0x68 },
	{ 0x052c, 0x4d },
	{ 0x0620, 0x9e },
	{ 0x0640, 0xe },
	{ 0x0644, 0x12 },
	{ 0x0648, 0x15 },
	{ 0x064c, 0x19 },
	{ 0x0678, 0x8 },
	{ 0x0550, 0x3f },
	{ 0x0554, 0x18 },
};

/* HS-G4 overrides. */
static const struct ufs_phy_reg phy_cal_table_rate_A_g4[] = {
	{ 0x0474, 0x4 },
	{ 0x04bc, 0x4 },
};

/* Second lane enable. */
static const struct ufs_phy_reg phy_cal_table_2nd_lane[] = {
	{ 0x05fc, 0x2 },
};

/* Rate B override. */
static const struct ufs_phy_reg phy_cal_table_rate_B[] = {
	{ 0x0420, 0x41 },
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static inline void phy_wr(volatile uint32_t *mmio, uint32_t reg, uint32_t val)
{
	mmio[reg >> 2] = val;
}

static inline uint32_t phy_rd(volatile uint32_t *mmio, uint32_t reg)
{
	return mmio[reg >> 2];
}

void ufs_qcom_phy_write_tbl(volatile uint32_t *mmio,
	const struct ufs_phy_reg *tbl, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		phy_wr(mmio, tbl[i].reg, tbl[i].val);
	}
}

/* PCS start: set bit 0 of register 0x400 and make it visible before polling. */
void ufs_qcom_phy_qmp_v4_start_serdes(volatile uint32_t *mmio)
{
	uint32_t v = phy_rd(mmio, 0x400);

	phy_wr(mmio, 0x400, v | 1u);
#if defined(__aarch64__)
	__asm__ volatile("dsb sy" ::: "memory");
#else
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/* PCS ready: bit 0 of register 0x400, polled the way the vendor driver does. */
bool ufs_qcom_phy_qmp_v4_is_pcs_ready(volatile uint32_t *mmio)
{
	return (phy_rd(mmio, 0x400) & 1u) != 0;
}

/*
 * Program the PHY for the selected rate. HS-G5 is the default on this board;
 * the second lane and rate B overrides are applied after the main table, in
 * the same order as ufs_qcom_phy_qmp_v4_phy_calibrate().
 */
void ufs_qcom_phy_qmp_v4_calibrate(volatile uint32_t *mmio, bool second_lane,
	bool rate_b)
{
	ufs_qcom_phy_write_tbl(mmio, phy_cal_table_rate_A_g5,
	    ARRAY_SIZE(phy_cal_table_rate_A_g5));
	ufs_qcom_phy_write_tbl(mmio, phy_cal_table_rate_A_g4,
	    ARRAY_SIZE(phy_cal_table_rate_A_g4));
	if (second_lane) {
		ufs_qcom_phy_write_tbl(mmio, phy_cal_table_2nd_lane,
		    ARRAY_SIZE(phy_cal_table_2nd_lane));
	}
	if (rate_b) {
		ufs_qcom_phy_write_tbl(mmio, phy_cal_table_rate_B,
		    ARRAY_SIZE(phy_cal_table_rate_B));
	}
}

