/*
 * Copyright (c) 2025 MediaTek
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runtime UART reconfigure test (CONFIG_UART_USE_RUNTIME_CONFIGURE).
 *
 * Reads the console config back, then changes the baud rate on the wire and
 * restores it. Each phase is announced repeatedly so the host has a window to
 * resynchronise at the new rate.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void announce(const char *tag, int secs)
{
	for (int i = 0; i < secs * 4; i++) {
		printk("RECONF %s\n", tag);
		k_sleep(K_MSEC(250));
	}
}

static int set_baud(uint32_t baud)
{
	struct uart_config cfg;
	int rc = uart_config_get(uart_dev, &cfg);

	if (rc != 0) {
		return rc;
	}
	cfg.baudrate = baud;
	/* let the FIFO drain before the divisor changes under it */
	k_sleep(K_MSEC(100));
	return uart_configure(uart_dev, &cfg);
}

int main(void)
{
	struct uart_config cfg;
	int rc;

	printk("\nRECONF start %s\n", CONFIG_BOARD_TARGET);

	rc = uart_config_get(uart_dev, &cfg);
	printk("RECONF get rc=%d baud=%u parity=%u stop=%u data=%u flow=%u\n",
	       rc, cfg.baudrate, cfg.parity, cfg.stop_bits, cfg.data_bits, cfg.flow_ctrl);

	announce("phase1-115200", 3);

	rc = set_baud(9600);
	announce("phase2-9600", 4);

	rc = set_baud(115200);
	announce("phase3-115200-restored", 4);

	printk("RECONF done\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
