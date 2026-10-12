/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mediatek_mt8188_wdt

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/util.h>

/*
 * The status register of the top reset generation unit records what caused
 * the last reset.  Only the two causes below have a hwinfo flag; the other
 * bits report firmware-level causes.  A power-on clears the whole register.
 */
#define WDT_STA            0x0c
#define WDT_STA_HW_WDT_RST BIT(31)
#define WDT_STA_SW_WDT_RST BIT(30)

#define SUPPORTED_CAUSES (RESET_POR | RESET_WATCHDOG | RESET_SOFTWARE)

/* The causes read at boot; clearing them changes this copy only. */
static atomic_t reset_cause;

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	*cause = (uint32_t)atomic_get(&reset_cause);

	return 0;
}

int z_impl_hwinfo_clear_reset_cause(void)
{
	(void)atomic_clear(&reset_cause);

	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = SUPPORTED_CAUSES;

	return 0;
}

static int hwinfo_mt8188_init(void)
{
	mm_reg_t base;
	uint32_t status;
	uint32_t cause = 0U;

	device_map(&base, DT_INST_REG_ADDR(0), DT_INST_REG_SIZE(0), K_MEM_CACHE_NONE);
	status = sys_read32(base + WDT_STA);

	if (status == 0U) {
		cause = RESET_POR;
	}
	if ((status & WDT_STA_HW_WDT_RST) != 0U) {
		cause |= RESET_WATCHDOG;
	}
	if ((status & WDT_STA_SW_WDT_RST) != 0U) {
		cause |= RESET_SOFTWARE;
	}
	(void)atomic_set(&reset_cause, (atomic_val_t)cause);

	return 0;
}

SYS_INIT(hwinfo_mt8188_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
