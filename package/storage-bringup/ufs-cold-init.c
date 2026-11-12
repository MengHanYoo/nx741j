// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Board cold-start path for the UFS controller on nx741j (SM8850, QMP v4 canoe PHY).
//
// Gated by native-ufs-cold-init=1 so the firmware takeover stays the default:
//
//   calibrate (106+2+1+1 register writes) -> start_serdes (PCS 0x400 bit 0)
//   -> poll PCS ready -> HCE=1 -> UIC DME_LINKSTARTUP -> UPMCRS=PWR_LOCAL
//
// The PHY tables come from ufs-qcom-phy.c, which is a byte-for-byte dump of the
// vendor phy-qcom-ufs-qmp-v4-canoe module. Only the PHY and the host controller
// registers are touched; the request lists stay owned by ShengUfs.
extern void panic(const char *fmt, ...) __attribute__((noreturn));
#include <stdint.h>

#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>

#include "ufs-qcom-phy.c"

#define UFS_PHY_BASE           0x1d80000ull
#define UFS_PHY_BYTES          0x1000u
#define UFS_HOST_BASE          0x1d84000ull
#define UFS_HOST_BYTES         0x3000u

#define HOST_IS                0x20u
#define HOST_HCS               0x30u
#define HOST_HCE               0x34u
#define HOST_UICCMD            0x90u

#define IS_UCCS                (1u << 10)
#define HCS_UCRDY              (1u << 3)
#define UIC_DME_LINKSTARTUP    0x16u
#define POLL_STEP_US           50u
#define POLL_BUDGET            2000u

static inline uint32_t host_read(volatile uint32_t *host, uint32_t offset)
{
	return host[offset >> 2];
}

static inline void host_write(volatile uint32_t *host, uint32_t offset, uint32_t value)
{
	host[offset >> 2] = value;
}

static int poll_bit(volatile uint32_t *base, uint32_t offset, uint32_t mask)
{
	for (unsigned i = 0; i < POLL_BUDGET; i++) {
		if ((host_read(base, offset) & mask) != 0) {
			return 0;
		}
		delay((int)POLL_STEP_US);
	}
	return -1;
}

static int uic_command(volatile uint32_t *host, uint32_t command, uint32_t arg1,
	uint32_t *result)
{
	host_write(host, HOST_IS, 0xffffffffu);
	host_write(host, HOST_UICCMD + 4, arg1);
	host_write(host, HOST_UICCMD + 8, 0);
	host_write(host, HOST_UICCMD + 12, 0);
	host_write(host, HOST_UICCMD, command);
	if (poll_bit(host, HOST_IS, IS_UCCS) != 0) {
		return -1;
	}
	*result = host_read(host, HOST_UICCMD + 8);
	host_write(host, HOST_IS, IS_UCCS);
	return 0;
}

int board_ufs_cold_init(void)
{
	int enabled = 0;
	volatile uint32_t *phy;
	volatile uint32_t *host;
	uint32_t result = 0;
	uint32_t hcs;

	if (!PE_parse_boot_argn("native-ufs-cold-init", &enabled, sizeof enabled) ||
	    enabled != 1) {
		return 0;
	}

	phy = (volatile uint32_t *)ml_io_map(UFS_PHY_BASE, UFS_PHY_BYTES);
	host = (volatile uint32_t *)ml_io_map(UFS_HOST_BASE, UFS_HOST_BYTES);
	if (phy == 0 || host == 0) {
		panic("NX741J UFS cold: mmio map failed phy %p host %p", (void *)phy, (void *)host);
		return -1;
	}

	printf("UFS cold: phy %p host %p, calibrating\n", (void *)phy, (void *)host);
	ufs_qcom_phy_qmp_v4_calibrate(phy, true, false);
	ufs_qcom_phy_qmp_v4_start_serdes(phy);
	if (poll_bit(phy, 0x400u, 1u) != 0) {
		panic("NX741J UFS cold: PCS not ready (0x400 %08x)", phy[0x400u >> 2]);
		return -2;
	}
	printf("UFS cold: PCS ready (0x400 %08x)\n", phy[0x400u >> 2]);

	host_write(host, HOST_HCE, host_read(host, HOST_HCE) | 1u);
	if (poll_bit(host, HOST_HCS, HCS_UCRDY) != 0) {
		panic("NX741J UFS cold: HCE on, UCRDY never set (HCS %08x)",
		    host_read(host, HOST_HCS));
		return -3;
	}

	if (uic_command(host, UIC_DME_LINKSTARTUP, 0, &result) != 0) {
		panic("NX741J UFS cold: DME_LINKSTARTUP timeout (IS %08x)",
		    host_read(host, HOST_IS));
		return -4;
	}
	hcs = host_read(host, HOST_HCS);
	printf("UFS cold: link rc %u, HCS %08x UPMCRS %u\n", result & 0xffu, hcs,
	    (hcs >> 8) & 7u);
	if ((result & 0xffu) != 0 || ((hcs >> 8) & 7u) != 1u) {
	panic("NX741J UFS cold: link not up rc %u HCS %08x UPMCRS %u", result & 0xffu, hcs, (hcs >> 8) & 7u);
		return -5;
	}
	panic("NX741J UFS cold: link up rc %u HCS %08x UPMCRS %u PCS %08x", result & 0xffu, hcs, (hcs >> 8) & 7u, phy[0x400u >> 2]);
	return 0;
}
