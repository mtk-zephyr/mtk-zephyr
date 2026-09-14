/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mediatek_mt8188_eint

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/interrupt_controller/intc_mtk_eint.h>
#include <zephyr/irq.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

/*
 * Every register group holds one bit per line, so a line picks a word within
 * its group and a bit within that word.  Writing a bit to the SET or CLR alias
 * of a group changes that bit alone, which is what lets this driver touch a
 * single line without a read-modify-write and therefore without a lock.
 *
 * MASK is inverted with respect to the API: a set bit stops the line.
 * SENS selects level rather than edge detection, POL selects high rather than
 * low, and between them they give the four conditions in enum eint_mtk_trigger.
 */
#define EINT_STA      0x000
#define EINT_ACK      0x040
#define EINT_MASK     0x080
#define EINT_MASK_SET 0x0c0
#define EINT_MASK_CLR 0x100
#define EINT_SENS     0x140
#define EINT_SENS_SET 0x180
#define EINT_SENS_CLR 0x1c0
#define EINT_POL      0x300
#define EINT_POL_SET  0x340
#define EINT_POL_CLR  0x380

/* Lines held in one register of a group. */
#define EINT_LINES_PER_WORD 32U

struct eint_mt8188_config {
	DEVICE_MMIO_ROM; /* Must be first. */
	void (*irq_config)(void);
	uint16_t num_lines;
};

struct eint_mt8188_data {
	DEVICE_MMIO_RAM; /* Must be first. */
	struct k_spinlock lock;
	sys_slist_t callbacks;
};

static inline uint32_t eint_mt8188_word(uint8_t line)
{
	return (line / EINT_LINES_PER_WORD) * sizeof(uint32_t);
}

static inline uint32_t eint_mt8188_bit(uint8_t line)
{
	return BIT(line % EINT_LINES_PER_WORD);
}

static void eint_mt8188_set(const struct device *dev, uint32_t group, uint8_t line)
{
	sys_write32(eint_mt8188_bit(line), DEVICE_MMIO_GET(dev) + group + eint_mt8188_word(line));
}

static uint32_t eint_mt8188_get(const struct device *dev, uint32_t group, uint8_t line)
{
	return sys_read32(DEVICE_MMIO_GET(dev) + group + eint_mt8188_word(line));
}

int eint_mtk_init_callback(eint_mtk_callback_t *callback, uint8_t first_line, uint8_t num_lines,
			   eint_mtk_cb_handler_t cb_handler, const struct device *cb_dev,
			   void *cb_arg)
{
	if ((callback == NULL) || (cb_handler == NULL) || (cb_dev == NULL) || (num_lines == 0U)) {
		return -EINVAL;
	}

	callback->cb_handler = cb_handler;
	callback->cb_dev = cb_dev;
	callback->cb_arg = cb_arg;
	callback->first_line = first_line;
	callback->num_lines = num_lines;

	return 0;
}

int eint_mtk_add_callback(const struct device *dev, eint_mtk_callback_t *callback)
{
	const struct eint_mt8188_config *config = dev->config;
	struct eint_mt8188_data *data = dev->data;
	eint_mtk_callback_t *registered;
	k_spinlock_key_t key;
	int ret = 0;

	if ((callback == NULL) || (callback->cb_handler == NULL) || (callback->cb_dev == NULL) ||
	    (callback->num_lines == 0U)) {
		return -EINVAL;
	}

	if (((uint32_t)callback->first_line + callback->num_lines) > config->num_lines) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	/*
	 * Reject an overlap rather than letting two consumers share a line: the
	 * second registration would never be reached, because dispatch stops at
	 * the first range that contains the line.
	 */
	SYS_SLIST_FOR_EACH_CONTAINER(&data->callbacks, registered, node) {
		if ((callback->first_line <
		     ((uint32_t)registered->first_line + registered->num_lines)) &&
		    (registered->first_line <
		     ((uint32_t)callback->first_line + callback->num_lines))) {
			ret = -EINVAL;
			break;
		}
	}

	if (ret == 0) {
		sys_slist_prepend(&data->callbacks, &callback->node);
	}

	k_spin_unlock(&data->lock, key);

	return ret;
}

void eint_mtk_remove_callback(const struct device *dev, eint_mtk_callback_t *callback)
{
	struct eint_mt8188_data *data = dev->data;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->lock);

	(void)sys_slist_find_and_remove(&data->callbacks, &callback->node);

	k_spin_unlock(&data->lock, key);
}

int eint_mtk_enable(const struct device *dev, uint8_t line)
{
	const struct eint_mt8188_config *config = dev->config;

	if (line >= config->num_lines) {
		return -EINVAL;
	}

	/* A set mask bit stops the line, so enabling clears it. */
	eint_mt8188_set(dev, EINT_MASK_CLR, line);

	return 0;
}

int eint_mtk_disable(const struct device *dev, uint8_t line)
{
	const struct eint_mt8188_config *config = dev->config;

	if (line >= config->num_lines) {
		return -EINVAL;
	}

	eint_mt8188_set(dev, EINT_MASK_SET, line);

	return 0;
}

bool eint_mtk_is_enabled(const struct device *dev, uint8_t line)
{
	const struct eint_mt8188_config *config = dev->config;

	if (line >= config->num_lines) {
		return false;
	}

	return (eint_mt8188_get(dev, EINT_MASK, line) & eint_mt8188_bit(line)) == 0U;
}

int eint_mtk_set_trigger(const struct device *dev, uint8_t line, enum eint_mtk_trigger trig)
{
	const struct eint_mt8188_config *config = dev->config;
	uint32_t sens;
	uint32_t pol;

	if (line >= config->num_lines) {
		return -EINVAL;
	}

	switch (trig) {
	case EINT_MTK_TRIG_EDGE_RISING:
		sens = EINT_SENS_CLR;
		pol = EINT_POL_SET;
		break;

	case EINT_MTK_TRIG_EDGE_FALLING:
		sens = EINT_SENS_CLR;
		pol = EINT_POL_CLR;
		break;

	case EINT_MTK_TRIG_LEVEL_HIGH:
		sens = EINT_SENS_SET;
		pol = EINT_POL_SET;
		break;

	case EINT_MTK_TRIG_LEVEL_LOW:
		sens = EINT_SENS_SET;
		pol = EINT_POL_CLR;
		break;

	default:
		return -EINVAL;
	}

	eint_mt8188_set(dev, sens, line);
	eint_mt8188_set(dev, pol, line);

	/*
	 * Changing the condition can itself latch a status bit - inverting the
	 * polarity of a line that is already at the new active level looks like
	 * an edge - so drop whatever is pending.  Otherwise a line enabled
	 * after this call fires once for a condition that predates it.
	 */
	eint_mt8188_set(dev, EINT_ACK, line);

	return 0;
}

static void eint_mt8188_dispatch(const struct device *dev, uint8_t line)
{
	struct eint_mt8188_data *data = dev->data;
	eint_mtk_cb_handler_t handler = NULL;
	eint_mtk_callback_t *callback;
	const struct device *cb_dev = NULL;
	void *cb_arg = NULL;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->lock);

	SYS_SLIST_FOR_EACH_CONTAINER(&data->callbacks, callback, node) {
		if ((line >= callback->first_line) &&
		    (line < ((uint32_t)callback->first_line + callback->num_lines))) {
			handler = callback->cb_handler;
			cb_dev = callback->cb_dev;
			cb_arg = callback->cb_arg;
			break;
		}
	}

	k_spin_unlock(&data->lock, key);

	/*
	 * Called with the lock dropped: a handler is free to reconfigure its
	 * lines, and one that registered a further range would otherwise
	 * deadlock on a lock this driver already holds.
	 */
	if (handler != NULL) {
		handler(cb_dev, line, cb_arg);
	}
}

static void eint_mt8188_isr(const struct device *dev)
{
	const struct eint_mt8188_config *config = dev->config;
	uint16_t base;

	for (base = 0U; base < config->num_lines; base += EINT_LINES_PER_WORD) {
		uint16_t remaining = config->num_lines - base;
		uint32_t word = (base / EINT_LINES_PER_WORD) * sizeof(uint32_t);
		uint32_t status = sys_read32(DEVICE_MMIO_GET(dev) + EINT_STA + word);

		/*
		 * The last word is only partly populated, and the bits above the
		 * last line read back as whatever the block leaves there.
		 */
		if (remaining < EINT_LINES_PER_WORD) {
			status &= BIT_MASK(remaining);
		}

		while (status != 0U) {
			uint32_t bit = find_lsb_set(status) - 1U;

			status &= ~BIT(bit);

			/*
			 * Acknowledge before the handler runs.  An edge that
			 * arrives while it is running then leaves the status bit
			 * set and the line fires again, rather than being
			 * cleared afterwards and lost.
			 */
			sys_write32(BIT(bit), DEVICE_MMIO_GET(dev) + EINT_ACK + word);

			eint_mt8188_dispatch(dev, (uint8_t)(base + bit));
		}
	}
}

static int eint_mt8188_init(const struct device *dev)
{
	const struct eint_mt8188_config *config = dev->config;
	struct eint_mt8188_data *data = dev->data;
	uint16_t line;

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	sys_slist_init(&data->callbacks);

	/*
	 * The block keeps its state across a warm reset and this cell does not
	 * own the boot path, so start from every line stopped and nothing
	 * pending rather than from whatever the previous owner left behind.
	 */
	for (line = 0U; line < config->num_lines; line += EINT_LINES_PER_WORD) {
		uint16_t remaining = config->num_lines - line;
		uint32_t word = (line / EINT_LINES_PER_WORD) * sizeof(uint32_t);
		uint32_t lines = UINT32_MAX;

		/* Keep off the bits above the last line of a partial word. */
		if (remaining < EINT_LINES_PER_WORD) {
			lines = BIT_MASK(remaining);
		}

		sys_write32(lines, DEVICE_MMIO_GET(dev) + EINT_MASK_SET + word);
		sys_write32(lines, DEVICE_MMIO_GET(dev) + EINT_ACK + word);
	}

	config->irq_config();

	return 0;
}

#define EINT_MT8188_INIT(n)                                                                        \
	static void eint_mt8188_irq_config_##n(void)                                               \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), eint_mt8188_isr,            \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
                                                                                                   \
	static struct eint_mt8188_data eint_mt8188_data_##n;                                       \
                                                                                                   \
	static const struct eint_mt8188_config eint_mt8188_config_##n = {                          \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)),                                              \
		.irq_config = eint_mt8188_irq_config_##n,                                          \
		.num_lines = DT_INST_PROP(n, num_lines),                                           \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, eint_mt8188_init, NULL, &eint_mt8188_data_##n,                    \
			      &eint_mt8188_config_##n, PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY,    \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(EINT_MT8188_INIT)
