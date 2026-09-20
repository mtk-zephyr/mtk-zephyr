/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MediaTek MT8188 Audio Front End interface.
 *
 * The AFE moves samples between memory and the die's eTDM serial ports.  A
 * caller describes a stream with mt8188_afe_cfg, points a memory interface at
 * a ring buffer, connects it to a port, and starts it.
 *
 * This is not the Zephyr DAI interface.  The AFE's routing matrix, its
 * channel-merge units and its cowork'd port pairs have no expression in DAI,
 * so the driver carries its own interface until that changes.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_AUDIO_MT8188_AFE_H_
#define ZEPHYR_INCLUDE_DRIVERS_AUDIO_MT8188_AFE_H_

#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Channels one eTDM port can carry.  A wider stream spans a port pair. */
#define MT8188_ETDM_MAX_CHANNELS 16

/** Widest stream the AFE accepts, being a cowork'd pair of eTDM ports. */
#define MT8188_AFE_MAX_CHANNELS (2 * MT8188_ETDM_MAX_CHANNELS)

/** The master clock is an input; an external master drives the pin. */
#define MT8188_MCLK_DIR_IN  0
/** The master clock is an output driven by this die. */
#define MT8188_MCLK_DIR_OUT 1

/** @brief Serial framing on an eTDM port. */
enum mt8188_etdm_fmt {
	/** I2S, in which the frame clock idles low. */
	MT8188_ETDM_FMT_I2S = 0,
	/** Right justified. */
	MT8188_ETDM_FMT_RJ = 1,
	/** Left justified. */
	MT8188_ETDM_FMT_LJ = 2,
	/** EIAJ CP-1201. */
	MT8188_ETDM_FMT_EIAJ = 3,
	/** DSP-A. */
	MT8188_ETDM_FMT_DSPA = 4,
	/** DSP-B. */
	MT8188_ETDM_FMT_DSPB = 5,
};

/** @brief How a stream's channels are spread over the data pins. */
enum mt8188_etdm_data_mode {
	/** One pin carries every channel as a TDM stream. */
	MT8188_ETDM_DATA_ONE_PIN = 0,
	/** Each pin carries two channels. */
	MT8188_ETDM_DATA_MULTI_PIN = 1,
};

/** @brief Memory interfaces.  DL plays out of memory, UL captures into it. */
enum mt8188_memif_id {
	MT8188_DL2 = 0,
	MT8188_DL3,
	MT8188_DL6,
	MT8188_DL7,
	MT8188_DL8,
	MT8188_DL10,
	MT8188_DL11,
	MT8188_UL1,
	MT8188_UL2,
	MT8188_UL3,
	MT8188_UL4,
	MT8188_UL5,
	MT8188_UL6,
	MT8188_UL8,
	MT8188_UL9,
	MT8188_UL10,
	/** Number of memory interfaces. */
	MT8188_MEMIF_NR,
};

/** @brief eTDM ports. */
enum mt8188_etdm_id {
	MT8188_ETDM_OUT1 = 0,
	MT8188_ETDM_OUT2,
	MT8188_ETDM_IN1,
	MT8188_ETDM_IN2,
	/** Number of eTDM ports. */
	MT8188_ETDM_NR,
};

/** @brief Where a route starts. */
enum mt8188_route_src {
	/** Capture from eTDM_IN1. */
	MT8188_ROUTE_SRC_ETDM_IN1 = 0,
	/** Capture from eTDM_IN2. */
	MT8188_ROUTE_SRC_ETDM_IN2,
	/** Capture from eTDM_IN1 and eTDM_IN2 merged, for a 32-channel stream. */
	MT8188_ROUTE_SRC_ETDM_IN1_IN2,
	/** Playback from DL8. */
	MT8188_ROUTE_SRC_DL8,
	/** Playback from DL11. */
	MT8188_ROUTE_SRC_DL11,
	/** Number of route sources. */
	MT8188_ROUTE_SRC_NR,
};

/** @brief Where a route ends. */
enum mt8188_route_dst {
	/** Capture into UL3. */
	MT8188_ROUTE_DST_UL3 = 0,
	/** Capture into UL8. */
	MT8188_ROUTE_DST_UL8,
	/** Capture into UL9. */
	MT8188_ROUTE_DST_UL9,
	/** Playback to eTDM_OUT1. */
	MT8188_ROUTE_DST_ETDM_OUT1,
	/** Playback to eTDM_OUT2. */
	MT8188_ROUTE_DST_ETDM_OUT2,
	/** Playback to eTDM_OUT1 and eTDM_OUT2, for a 32-channel stream. */
	MT8188_ROUTE_DST_ETDM_OUT1_OUT2,
	/** Number of route destinations. */
	MT8188_ROUTE_DST_NR,
};

/** @brief A stream's sample format and framing. */
struct mt8188_afe_cfg {
	/** Sample rate in Hz.  One of the rates the AFE has a timing value for. */
	uint32_t rate;
	/** Channels, from 1 to @ref MT8188_AFE_MAX_CHANNELS. */
	uint32_t channels;
	/** Bits per slot.  16 or 32. */
	uint8_t word_size;
	/** Serial framing on the port. */
	enum mt8188_etdm_fmt fmt;
	/** How the channels are spread over the data pins. */
	enum mt8188_etdm_data_mode data_mode;
	/**
	 * Take the bit and frame clocks from this port's own pins, an external
	 * master having been wired to them.  Capture only: eTDM_OUT1 is always
	 * a master, and the slave side of a cowork'd pair is set up by the
	 * driver rather than requested here.
	 */
	bool slave_mode;
	/** TDM slots per frame, or 0 to use @ref mt8188_afe_cfg.channels. */
	uint32_t slots;
	/** Frame clock width in bit clocks, or 0 to derive it. */
	uint32_t lrck_width;
	/**
	 * Master clock in Hz, or 0 for none.  Must divide the audio PLL that
	 * serves this sample rate by no more than 256.
	 */
	uint32_t mclk_freq;
	/** @ref MT8188_MCLK_DIR_IN or @ref MT8188_MCLK_DIR_OUT. */
	int mclk_dir;
	/**
	 * Frames between period callbacks, for example 480 for 10 ms at
	 * 48 kHz.  0 leaves the hardware's period counter alone.
	 */
	uint32_t period_frames;
};

/**
 * @brief Called from interrupt context when a period of frames has elapsed.
 *
 * @param data Argument given to mt8188_afe_set_period_cb().
 */
typedef void (*mt8188_afe_period_cb_t)(void *data);

/** @cond INTERNAL_HIDDEN */
__subsystem struct mt8188_afe_driver_api {
	int (*configure)(const struct device *dev, enum mt8188_memif_id memif_id,
			 const struct mt8188_afe_cfg *cfg);
	int (*route)(const struct device *dev, enum mt8188_route_src src,
		     enum mt8188_route_dst dst);
	int (*start)(const struct device *dev, enum mt8188_memif_id memif_id);
	int (*stop)(const struct device *dev, enum mt8188_memif_id memif_id);
	int (*set_buf)(const struct device *dev, enum mt8188_memif_id memif_id, uint32_t base,
		       uint32_t buf_size);
	uint32_t (*get_cur)(const struct device *dev, enum mt8188_memif_id memif_id);
	int (*set_period_cb)(const struct device *dev, enum mt8188_memif_id memif_id,
			     mt8188_afe_period_cb_t cb, void *cb_data);
};
/** @endcond */

/**
 * @brief Describe a stream on a memory interface.
 *
 * Configures the interface and the eTDM port it is wired to.  Call before
 * mt8188_afe_start().
 *
 * @param dev      AFE device.
 * @param memif_id Memory interface to configure.
 * @param cfg      Sample format and framing.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the interface does not exist, or the rate, channel count,
 *                 word size or master clock is not one the AFE can produce.
 * @retval -ENOTSUP if the interface exists but this driver does not drive it.
 */
static inline int mt8188_afe_configure(const struct device *dev, enum mt8188_memif_id memif_id,
				       const struct mt8188_afe_cfg *cfg)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->configure(dev, memif_id, cfg);
}

/**
 * @brief Connect a source to a destination through the routing matrix.
 *
 * Replaces whatever the previous route for @p dst was.  Call before
 * mt8188_afe_start().
 *
 * @param dev AFE device.
 * @param src Where the stream comes from.
 * @param dst Where it goes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if either endpoint does not exist.
 * @retval -ENOTSUP if the two cannot be connected on this die.
 */
static inline int mt8188_afe_route(const struct device *dev, enum mt8188_route_src src,
				   enum mt8188_route_dst dst)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->route(dev, src, dst);
}

/**
 * @brief Give a memory interface its ring buffer.
 *
 * The buffer must be reachable by the AFE, which does not share the CPU's
 * caches: place it in a non-cacheable region the AFE is allowed to address,
 * and pass its physical address.
 *
 * @param dev      AFE device.
 * @param memif_id Memory interface.
 * @param base     Physical address of the buffer.
 * @param buf_size Size in bytes.  A multiple of 16, and at least 16.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the interface does not exist, the size is zero or not a
 *                 multiple of 16, the address is not 16-byte aligned, or the
 *                 buffer wraps past the end of the address space.
 * @retval -ENOTSUP if the interface exists but this driver does not drive it.
 */
static inline int mt8188_afe_set_buf(const struct device *dev, enum mt8188_memif_id memif_id,
				     uint32_t base, uint32_t buf_size)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->set_buf(dev, memif_id, base, buf_size);
}

/**
 * @brief Register a period-elapsed callback for a memory interface.
 *
 * The callback runs in interrupt context.  Pass NULL to remove one.
 *
 * @param dev      AFE device.
 * @param memif_id Memory interface.
 * @param cb       Callback, or NULL.
 * @param cb_data  Argument passed to the callback.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the interface does not exist.
 * @retval -ENOTSUP if the interface exists but this driver does not drive it.
 */
static inline int mt8188_afe_set_period_cb(const struct device *dev, enum mt8188_memif_id memif_id,
					   mt8188_afe_period_cb_t cb, void *cb_data)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->set_period_cb(dev, memif_id, cb, cb_data);
}

/**
 * @brief Start moving samples on a memory interface.
 *
 * Requires mt8188_afe_configure() and mt8188_afe_set_buf() to have succeeded
 * for this interface.  On failure nothing is left running.
 *
 * @param dev      AFE device.
 * @param memif_id Memory interface.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the interface does not exist.
 * @retval -ENOTSUP if the interface exists but this driver does not drive it.
 * @retval -EPERM if the interface has no buffer, or was never configured.
 * @retval -EALREADY if it is already running.
 */
static inline int mt8188_afe_start(const struct device *dev, enum mt8188_memif_id memif_id)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->start(dev, memif_id);
}

/**
 * @brief Stop moving samples on a memory interface.
 *
 * @param dev      AFE device.
 * @param memif_id Memory interface.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the interface does not exist.
 * @retval -ENOTSUP if the interface exists but this driver does not drive it.
 * @retval -EALREADY if it is not running.
 */
static inline int mt8188_afe_stop(const struct device *dev, enum mt8188_memif_id memif_id)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->stop(dev, memif_id);
}

/**
 * @brief Read where the AFE has reached in the ring buffer.
 *
 * @param dev      AFE device.
 * @param memif_id Memory interface.
 *
 * @return Physical address the hardware is reading from or writing to, or 0 if
 *         the interface does not exist or this driver does not drive it.
 */
static inline uint32_t mt8188_afe_get_cur(const struct device *dev, enum mt8188_memif_id memif_id)
{
	const struct mt8188_afe_driver_api *api = (const struct mt8188_afe_driver_api *)dev->api;

	return api->get_cur(dev, memif_id);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_AUDIO_MT8188_AFE_H_ */
