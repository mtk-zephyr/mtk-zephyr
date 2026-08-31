/*
 * Copyright (c) 2025 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/arm_mmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

static const struct arm_mmu_region mmu_regions[] = {
	/*
	 * The interrupt controller driver reaches the GIC through flat physical
	 * addresses during early boot, before any runtime mapping exists, so
	 * both of its register banks need a static mapping here.
	 */
	MMU_REGION_FLAT_ENTRY("GIC_DIST", DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 0),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 0),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GIC_REDIST", DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 1),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 1),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	/*
	 * The pin controller is reached through a fixed base address rather
	 * than DEVICE_MMIO, so it needs a static mapping.  Peripherals that
	 * map their own registers at runtime do not appear here.
	 */
	MMU_REGION_FLAT_ENTRY("PINCTRL", DT_REG_ADDR(DT_NODELABEL(pinctrl)),
			      DT_REG_SIZE(DT_NODELABEL(pinctrl)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
