/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	printk("cntfrq = %u\n", sys_clock_hw_cycles_per_sec());

	/*
	 * Timer accuracy: k_uptime_get() derives from the same timer, so it
	 * cannot detect a misconfigured one. Print markers instead and measure
	 * the gap against a wall clock on the host.
	 */
	for (int i = 0; i < 3; i++) {
		printk("SLEEP_START %d\n", i);
		k_sleep(K_SECONDS(5));
		printk("SLEEP_END   %d\n", i);
	}

	printk("DONE\n");
	return 0;
}
