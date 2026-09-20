/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MT8188 AFE driver internals.  The interface callers use is in
 * <zephyr/drivers/audio/mt8188_afe.h>.
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_MT8188_AFE_PRIV_H_
#define ZEPHYR_DRIVERS_AUDIO_MT8188_AFE_PRIV_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/audio/mt8188_afe.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/device_mmio.h>

/*
 * Cowork sync-source values, as the hardware numbers them.  The _M and _S
 * suffixes name which clock inside a port to tap: _M is the one it generates
 * as a master, _S the one it receives on its own pins.
 */
enum mt8188_etdm_cowork_id {
	MT8188_COWORK_ETDM_NONE = 0,
	MT8188_COWORK_ETDM_IN1_M = 2,
	MT8188_COWORK_ETDM_IN1_S = 3,
	MT8188_COWORK_ETDM_IN2_M = 4,
	MT8188_COWORK_ETDM_IN2_S = 5,
	MT8188_COWORK_ETDM_OUT1_M = 10,
	MT8188_COWORK_ETDM_OUT1_S = 11,
	MT8188_COWORK_ETDM_OUT2_M = 12,
	MT8188_COWORK_ETDM_OUT2_S = 13,
};

/*
 * "This port has no cowork master", stored in
 * struct mt8188_etdm_config::cowork_source_id.
 *
 * It must not collide with any enum mt8188_etdm_id value, so it cannot be 0:
 * MT8188_ETDM_OUT1 is 0, so using MT8188_COWORK_ETDM_NONE here made a port
 * slaved to eTDM_OUT1 indistinguishable from an unslaved one, and
 * mt8188_etdm_update_sync_info() then dropped it so the slave port was never
 * configured at all.
 */
#define MT8188_COWORK_SOURCE_NONE (-1)

/* Cowork sync-source selectors, as the hardware numbers them. */
#define MT8188_ETDM_SYNC_NONE      0
#define MT8188_ETDM_SYNC_FROM_IN1  2
#define MT8188_ETDM_SYNC_FROM_IN2  4
#define MT8188_ETDM_SYNC_FROM_OUT1 10
#define MT8188_ETDM_SYNC_FROM_OUT2 12

/* Frame-clock timing tokens for the input FIFOs and the output relatch. */
#define MT8188_ETDM_OUT1_1X_EN 9
#define MT8188_ETDM_OUT2_1X_EN 10
#define MT8188_ETDM_IN1_1X_EN  12
#define MT8188_ETDM_IN2_1X_EN  13

/* Timing tokens for a memory interface wired straight to an eTDM input. */
#define MT8188_ETDM_IN1_NX_EN 25
#define MT8188_ETDM_IN2_NX_EN 26

/* Fastest bit clock an eTDM port will accept. */
#define MT8188_ETDM_NORMAL_MAX_BCK_RATE 24576000U

/* Audio PLLs, indexed as the driver counts them.  APLL1 serves the 48 kHz
 * sample-rate family and APLL2 the 44.1 kHz one.
 */
#define MT8188_APLL1   0
#define MT8188_APLL2   1
#define MT8188_APLL_NR 2

/* Alignment the AFE requires of a ring buffer's address and size. */
#define MT8188_AFE_BUF_ALIGN 16U

/* Per-eTDM-port configuration. */
struct mt8188_etdm_config {
	enum mt8188_etdm_data_mode data_mode;
	bool slave_mode;
	bool lrck_inv;
	bool bck_inv;
	uint32_t rate;
	enum mt8188_etdm_fmt fmt;
	/** TDM slots per frame, 0 to use the stream's channel count. */
	uint32_t slots;
	/** Frame clock width in bit clocks, 0 to derive it. */
	uint32_t lrck_width;
	/** Master clock in Hz, 0 for none. */
	uint32_t mclk_freq;
	/** Audio PLL the master clock is divided from, MT8188_APLL1 or _APLL2. */
	uint32_t mclk_apll;
	/** Whether that PLL choice was pinned by mt8188_etdm_cal_mclk(). */
	bool mclk_apll_valid;
	int mclk_dir;
	/** enum mt8188_etdm_id of the cowork master, or MT8188_COWORK_SOURCE_NONE. */
	int cowork_source_id;
	uint32_t cowork_slv_count;
	int cowork_slv_id[MT8188_ETDM_NR - 1];
	bool in_disable_ch[MT8188_ETDM_MAX_CHANNELS];
	bool configured;
};

/* What configure() recorded for a memory interface, for start() to use. */
struct mt8188_memif_rt {
	uint32_t period_frames;
	uint32_t rate;
	uint32_t channels;
	uint8_t word_size;
};

/* A memory interface's period callback. */
struct mt8188_memif_cb {
	mt8188_afe_period_cb_t cb;
	void *data;
};

/* Driver state. */
struct mt8188_afe {
	DEVICE_MMIO_RAM; /* Must be first. */
	/** Mapped AFE register base, cached from DEVICE_MMIO_GET() at init. */
	uintptr_t base;
	/** Mapped audio 26 MHz gate, which sits outside the AFE's own range. */
	uintptr_t adsp_audio26m;
	/** Mapped buffer region, when the devicetree names one. */
	uintptr_t dma_region;
	const struct device *clk_apmixed;
	const struct device *clk_topckgen;
	const struct device *clk_infra_ao;
	struct k_spinlock lock;
	struct mt8188_etdm_config etdm[MT8188_ETDM_NR];
	struct mt8188_memif_rt memif_rt[MT8188_MEMIF_NR];
	struct mt8188_memif_cb period_cb[MT8188_MEMIF_NR];
	/**
	 * Streams holding each audio PLL's timing domain.  The domain follows
	 * the sample rate, so two streams of the same rate family share one and
	 * the last to stop is the one that may tear it down.
	 */
	uint8_t apll_users[MT8188_APLL_NR];
	/** Interfaces configure() has described, one bit each. */
	uint32_t configured;
	/** Interfaces set_buf() has given a buffer, one bit each. */
	uint32_t buf_set;
	/** Interfaces start() has started, one bit each. */
	uint32_t started;
	/**
	 * Channel count start() used, per interface.  stop() has to release
	 * exactly the ports start() claimed, and a configure() in between can
	 * change how many that would be.
	 */
	uint32_t start_channels[MT8188_MEMIF_NR];
};

/* Clocks — mt8188_afe_clk.c */
int mt8188_afe_enable_reg_rw_clk(struct mt8188_afe *afe);
int mt8188_afe_disable_reg_rw_clk(struct mt8188_afe *afe);
void mt8188_afe_enable_main_clock(struct mt8188_afe *afe);
void mt8188_afe_disable_main_clock(struct mt8188_afe *afe);
int mt8188_apll1_enable(struct mt8188_afe *afe);
int mt8188_apll1_disable(struct mt8188_afe *afe);
int mt8188_apll2_enable(struct mt8188_afe *afe);
int mt8188_apll2_disable(struct mt8188_afe *afe);
int afe_enable_base_clocks(struct mt8188_afe *afe);
int afe_disable_base_clocks(struct mt8188_afe *afe);

/*
 * The audio PLL timing domain a sample rate needs.  Reference counted: a
 * stream may drive two eTDM ports but holds only one domain, and another
 * stream of the same rate family holds the same one.
 */
int mt8188_afe_enable_apll_domain(struct mt8188_afe *afe, uint32_t rate);
int mt8188_afe_disable_apll_domain(struct mt8188_afe *afe, uint32_t rate);

/* Which audio PLL a sample rate belongs to. */
int mt8188_afe_apll_by_rate(uint32_t rate);

/* Per-port clocks: master clock divider, its mux, and the port's gate. */
int mt8188_afe_enable_etdm_clocks(struct mt8188_afe *afe, enum mt8188_etdm_id id);
int mt8188_afe_disable_etdm_clocks(struct mt8188_afe *afe, enum mt8188_etdm_id id);

/* eTDM ports — mt8188_afe_etdm.c */
int mt8188_etdm_configure(struct mt8188_afe *afe, enum mt8188_etdm_id id,
			  const struct mt8188_afe_cfg *cfg);
int mt8188_etdm_set_fmt(struct mt8188_afe *afe, enum mt8188_etdm_id id, enum mt8188_etdm_fmt fmt,
			bool lrck_inv, bool bck_inv, bool slave_mode);
int mt8188_etdm_set_tdm_slot(struct mt8188_afe *afe, enum mt8188_etdm_id id, uint32_t slots,
			     uint32_t lrck_width);
int mt8188_etdm_set_sysclk(struct mt8188_afe *afe, enum mt8188_etdm_id id, uint32_t mclk_freq,
			   int mclk_dir);
int mt8188_etdm_cal_mclk(struct mt8188_afe *afe, enum mt8188_etdm_id id, uint32_t freq);
int mt8188_etdm_enable_mclk(struct mt8188_afe *afe, enum mt8188_etdm_id id);
int mt8188_etdm_disable_mclk(struct mt8188_afe *afe, enum mt8188_etdm_id id);
int mt8188_etdm_set_data_mode(struct mt8188_afe *afe, enum mt8188_etdm_id id,
			      enum mt8188_etdm_data_mode mode);
int mt8188_etdm_set_cowork_source(struct mt8188_afe *afe, enum mt8188_etdm_id id,
				  int cowork_source_id);
int mt8188_etdm_set_in_disable_ch(struct mt8188_afe *afe, enum mt8188_etdm_id id,
				  const uint8_t *disabled_chs, uint32_t count);
int mt8188_etdm_start(struct mt8188_afe *afe, enum mt8188_etdm_id id);
int mt8188_etdm_stop(struct mt8188_afe *afe, enum mt8188_etdm_id id);

/* Routing matrix — mt8188_afe_etdm.c */
int mt8188_afe_set_route(struct mt8188_afe *afe, enum mt8188_route_src src,
			 enum mt8188_route_dst dst);

/* Rebuild the slave-to-master cowork links from each port's stored source. */
void mt8188_etdm_update_sync_info(struct mt8188_afe *afe);

#endif /* ZEPHYR_DRIVERS_AUDIO_MT8188_AFE_PRIV_H_ */
