/*
 * Copyright (c) 2026 MediaTek Inc.
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
 * @brief Condition on which an external interrupt line fires.
 *
 * A line detects one condition at a time.  The controller has no dual-edge
 * mode, so a consumer that wants both edges selects the edge opposite to the
 * level the line currently sits at and selects it again after every event;
 * this enumeration deliberately offers no value for it.
 */
enum eint_mtk_trigger {
	/** Transition from low to high. */
	EINT_MTK_TRIG_EDGE_RISING,
	/** Transition from high to low. */
	EINT_MTK_TRIG_EDGE_FALLING,
	/** Line held high. */
	EINT_MTK_TRIG_LEVEL_HIGH,
	/** Line held low. */
	EINT_MTK_TRIG_LEVEL_LOW,
};

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

/**
 * @brief Fill in a registration for a range of lines.
 *
 * @param callback   Registration to initialise.
 * @param first_line First line of the range.
 * @param num_lines  Number of lines in the range, counted from @p first_line.
 * @param cb_handler Handler to call when a line in the range fires.
 * @param cb_dev     Device passed to the handler.
 * @param cb_arg     Argument passed to the handler.
 *
 * @retval 0 on success.
 * @retval -EINVAL if a required argument is NULL or the range is empty.
 */
int eint_mtk_init_callback(eint_mtk_callback_t *callback, uint8_t first_line, uint8_t num_lines,
			   eint_mtk_cb_handler_t cb_handler, const struct device *cb_dev,
			   void *cb_arg);

/**
 * @brief Register for events on a range of lines.
 *
 * Ranges may not overlap: one registration owns a given line.
 *
 * @param dev      External interrupt controller.
 * @param callback Registration filled in by eint_mtk_init_callback().
 *
 * @retval 0 on success.
 * @retval -EINVAL if the registration is malformed, reaches past the last line
 *                 of the controller, or overlaps one already registered.
 */
int eint_mtk_add_callback(const struct device *dev, eint_mtk_callback_t *callback);

/**
 * @brief Drop a registration.
 *
 * Lines the registration covered are left as they are; disable them first if
 * they should stop firing.
 *
 * @param dev      External interrupt controller.
 * @param callback Registration to drop.
 */
void eint_mtk_remove_callback(const struct device *dev, eint_mtk_callback_t *callback);

/**
 * @brief Let a line deliver events.
 *
 * @param dev  External interrupt controller.
 * @param line Line to enable.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the line is past the last one of the controller.
 */
int eint_mtk_enable(const struct device *dev, uint8_t line);

/**
 * @brief Stop a line delivering events.
 *
 * @param dev  External interrupt controller.
 * @param line Line to disable.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the line is past the last one of the controller.
 */
int eint_mtk_disable(const struct device *dev, uint8_t line);

/**
 * @brief Report whether a line may deliver events.
 *
 * @param dev  External interrupt controller.
 * @param line Line to query.
 *
 * @return true if the line is enabled, false if it is disabled or out of range.
 */
bool eint_mtk_is_enabled(const struct device *dev, uint8_t line);

/**
 * @brief Choose the condition on which a line fires.
 *
 * Any event already latched on the line is discarded, so a line enabled after
 * this call does not fire on a condition that predates it.  The line's enable
 * state is left alone.
 *
 * @param dev  External interrupt controller.
 * @param line Line to configure.
 * @param trig Condition to detect.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the line is past the last one of the controller, or the
 *                 condition is not one of @ref eint_mtk_trigger.
 */
int eint_mtk_set_trigger(const struct device *dev, uint8_t line, enum eint_mtk_trigger trig);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_MTK_EINT_H_ */
