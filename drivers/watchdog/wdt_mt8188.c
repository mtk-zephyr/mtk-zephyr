/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mediatek_mt8188_wdt

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/util.h>

/*
 * The watchdog registers of the top reset generation unit, used as Linux's
 * drivers/watchdog/mtk_wdt.c uses them.  The hardware ignores a write that
 * does not carry its register's key.  Every bit position the driver relies on
 * is named here and nowhere else.
 */
#define WDT_MODE          0x00
#define WDT_MODE_KEY      0x22000000U
#define WDT_MODE_KEY_MASK GENMASK(31, 24)
#define WDT_MODE_EN       BIT(0)
#define WDT_MODE_EXRST_EN BIT(2)
#define WDT_MODE_IRQ_EN   BIT(3)
#define WDT_MODE_DUAL_EN  BIT(6)

#define WDT_LENGTH             0x04
#define WDT_LENGTH_KEY         0x8U
#define WDT_LENGTH_TICKS_SHIFT 5

#define WDT_RESTART     0x08
#define WDT_RESTART_KEY 0x1971U

/* One step of the counter is 512 cycles of its 32 kHz clock. */
#define WDT_TICKS_PER_SEC 64U

/* The range of the timeout, as Linux's driver allows it. */
#define WDT_TIMEOUT_MIN_MS 2000U
#define WDT_TIMEOUT_MAX_MS 31000U

struct wdt_mt8188_config {
	DEVICE_MMIO_ROM;
	bool disable_extrst;
};

struct wdt_mt8188_data {
	DEVICE_MMIO_RAM;
	struct k_spinlock lock;
	/* LENGTH value of the installed timeout, key included; 0 if none. */
	uint32_t length;
	bool running;
};

static mm_reg_t wdt_mt8188_reg(const struct device *dev, uint32_t offset)
{
	return DEVICE_MMIO_GET(dev) + offset;
}

static void wdt_mt8188_update_mode(const struct device *dev, uint32_t set, uint32_t clear)
{
	uint32_t mode = sys_read32(wdt_mt8188_reg(dev, WDT_MODE));

	mode &= ~(WDT_MODE_KEY_MASK | clear);
	sys_write32(mode | set | WDT_MODE_KEY, wdt_mt8188_reg(dev, WDT_MODE));
}

static int wdt_mt8188_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	struct wdt_mt8188_data *data = dev->data;
	k_spinlock_key_t key;
	uint32_t ticks;
	int ret;

	if ((cfg->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_SOC || cfg->callback != NULL) {
		return -ENOTSUP;
	}

	if (cfg->window.min != 0U || cfg->window.max < WDT_TIMEOUT_MIN_MS ||
	    cfg->window.max > WDT_TIMEOUT_MAX_MS) {
		return -EINVAL;
	}

	/* Rounded up, so that the reset never comes before window.max. */
	ticks = DIV_ROUND_UP(cfg->window.max * WDT_TICKS_PER_SEC, 1000U);

	key = k_spin_lock(&data->lock);
	if (data->running) {
		ret = -EBUSY;
	} else if (data->length != 0U) {
		ret = -ENOMEM;
	} else {
		data->length = (ticks << WDT_LENGTH_TICKS_SHIFT) | WDT_LENGTH_KEY;
		ret = 0;
	}
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int wdt_mt8188_setup(const struct device *dev, uint8_t options)
{
	const struct wdt_mt8188_config *config = dev->config;
	struct wdt_mt8188_data *data = dev->data;
	k_spinlock_key_t key;
	uint32_t clear;
	int ret;

	/* The counter runs on through sleep and debug halts alike. */
	if (options != 0U) {
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);
	if (data->running) {
		ret = -EBUSY;
	} else if (data->length == 0U) {
		ret = -EINVAL;
	} else {
		sys_write32(data->length, wdt_mt8188_reg(dev, WDT_LENGTH));
		sys_write32(WDT_RESTART_KEY, wdt_mt8188_reg(dev, WDT_RESTART));

		/* Reset only: no interrupt, neither before the reset nor instead of it. */
		clear = WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN;
		if (config->disable_extrst) {
			clear |= WDT_MODE_EXRST_EN;
		}
		wdt_mt8188_update_mode(dev, WDT_MODE_EN, clear);

		data->running = true;
		ret = 0;
	}
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int wdt_mt8188_disable(const struct device *dev)
{
	struct wdt_mt8188_data *data = dev->data;
	k_spinlock_key_t key;
	int ret;

	key = k_spin_lock(&data->lock);
	/* The timeout is uninstalled whether or not it was started. */
	data->length = 0U;
	if (!data->running) {
		ret = -EFAULT;
	} else {
		wdt_mt8188_update_mode(dev, 0U, WDT_MODE_EN);
		data->running = false;
		ret = 0;
	}
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int wdt_mt8188_feed(const struct device *dev, int channel_id)
{
	struct wdt_mt8188_data *data = dev->data;
	k_spinlock_key_t key;
	int ret;

	if (channel_id != 0) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	if (!data->running) {
		ret = -EINVAL;
	} else {
		sys_write32(WDT_RESTART_KEY, wdt_mt8188_reg(dev, WDT_RESTART));
		ret = 0;
	}
	k_spin_unlock(&data->lock, key);

	return ret;
}

static DEVICE_API(wdt, wdt_mt8188_api) = {
	.setup = wdt_mt8188_setup,
	.disable = wdt_mt8188_disable,
	.install_timeout = wdt_mt8188_install_timeout,
	.feed = wdt_mt8188_feed,
};

static int wdt_mt8188_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	/*
	 * Otherwise the registers are left as the firmware left them until
	 * wdt_setup(): a watchdog that is already running keeps running.
	 */
	if (IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
		wdt_mt8188_update_mode(dev, 0U, WDT_MODE_EN);
	}

	return 0;
}

#define WDT_MT8188_INIT(n)                                                                         \
	static const struct wdt_mt8188_config wdt_mt8188_config_##n = {                            \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)),                                              \
		.disable_extrst = DT_INST_PROP(n, mediatek_disable_extrst),                        \
	};                                                                                         \
                                                                                                   \
	static struct wdt_mt8188_data wdt_mt8188_data_##n;                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, wdt_mt8188_init, NULL, &wdt_mt8188_data_##n,                      \
			      &wdt_mt8188_config_##n, PRE_KERNEL_1,                                \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &wdt_mt8188_api);

DT_INST_FOREACH_STATUS_OKAY(WDT_MT8188_INIT)
