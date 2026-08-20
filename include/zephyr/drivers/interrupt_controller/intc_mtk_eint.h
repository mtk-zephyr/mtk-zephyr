/*
 * Copyright (c) 2025 MediaTek
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_MTK_EINT_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_MTK_EINT_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Called when an external interrupt fires on a registered line.
 *
 * @param dev  Device that registered the callback.
 * @param line Line that fired, counted from the start of the controller.
 * @param arg  Argument supplied at registration.
 */
typedef void (*eint_mtk_cb_handler_t)(const struct device *dev, uint8_t line, void *arg);

/**
 * @brief Registration for a contiguous range of external interrupt lines.
 *
 * The consumer owns this storage and keeps it alive for as long as the callback
 * is registered.  Initialise it with eint_mtk_init_callback() rather than by
 * hand.
 */
typedef struct {
	sys_snode_t node;

	eint_mtk_cb_handler_t cb_handler;

	const struct device *cb_dev;

	void *cb_arg;

	uint8_t first_line;
	uint8_t num_lines;
} eint_mtk_callback_t;

int eint_mtk_init_callback(eint_mtk_callback_t *callback, uint8_t first_line, uint8_t num_lines,
			   eint_mtk_cb_handler_t cb_handler, const struct device *cb_dev,
			   void *cb_arg);
int eint_mtk_add_callback(const struct device *dev, eint_mtk_callback_t *callback);
void eint_mtk_remove_callback(const struct device *dev, eint_mtk_callback_t *callback);

int eint_mtk_enable(const struct device *dev, uint8_t line);
int eint_mtk_disable(const struct device *dev, uint8_t line);
bool eint_mtk_is_enabled(const struct device *dev, uint8_t line);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_MTK_EINT_H_ */
