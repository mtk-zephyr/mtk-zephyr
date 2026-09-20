/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MT8188 eTDM hardware configuration, AFE_CONN routing, and start/stop.
 * Ported from Linux sound/soc/mediatek/mt8188/mt8188-dai-etdm.c.
 *
 * Implements:
 *   - Full CON0–CON5 configuration (master and slave mode)
 *   - LRCK/BCK inversion
 *   - TDM slot / LRCK-width override
 *   - AFIFO setup for IN paths
 *   - Per-channel disable (CON3/CON5)
 *   - Cowork sync machinery (slave-sel + sync-sel in ETDM_COWORK_CON*)
 *   - Clock source selection (CON2/CON4 CLOCK field)
 *   - BCK rate validation
 *   - set_fmt / set_tdm_slot / set_sysclk equivalents as plain C functions
 *   - MCLK output flag in CON1
 *   - MCLK mux APLL reparent + divider programming (rate-selected APLL)
 *   - AFE_CONN routing table
 *
 * HDMI/DPTX (ETDM3_OUT) is deferred. MCLK divider ratio is programmed
 * directly (no CCF rounding/validation beyond the 8-bit field).
 */

#include <string.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/mtk_mt8188_clock.h>

#include "mt8188_afe_priv.h"
#include "mt8188_afe_reg.h"

/* -------------------------------------------------------------------------
 * FIELD_PREP / FIELD_GET (Zephyr provides GENMASK/BIT but not FIELD_PREP)
 * -------------------------------------------------------------------------
 */
#ifndef FIELD_PREP
#define FIELD_PREP(_mask, _val) (((uint32_t)(_val) << __builtin_ctz(_mask)) & (uint32_t)(_mask))
#endif
#ifndef FIELD_GET
#define FIELD_GET(_mask, _reg) (((uint32_t)(_reg) & (uint32_t)(_mask)) >> __builtin_ctz(_mask))
#endif

/* -------------------------------------------------------------------------
 * Register read-modify-write helper
 * -------------------------------------------------------------------------
 */
static void reg_update_bits(uintptr_t base, uint32_t reg, uint32_t mask, uint32_t val)
{
	uint32_t tmp = sys_read32(base + reg);

	tmp = (tmp & ~mask) | (val & mask);
	sys_write32(tmp, base + reg);
}

static void reg_set_bits(uintptr_t base, uint32_t reg, uint32_t bits)
{
	sys_write32(sys_read32(base + reg) | bits, base + reg);
}

/* -------------------------------------------------------------------------
 * eTDM register base addresses per port
 * -------------------------------------------------------------------------
 */
struct etdm_regs {
	uint32_t con0;
	uint32_t con1;
	uint32_t con2;
	uint32_t con3;
	uint32_t con4;
	uint32_t con5;
};

/*
 * The descriptor tables below are laid out by hand to mirror the datasheet.
 * They are excluded from clang-format because it moves the brace of a
 * designated initializer onto its own line, which checkpatch rejects.
 */
/* clang-format off */
static const struct etdm_regs etdm_reg_table[MT8188_ETDM_NR] = {
	[MT8188_ETDM_OUT1] = {
		.con0 = ETDM_OUT1_CON0, .con1 = ETDM_OUT1_CON1,
		.con2 = ETDM_OUT1_CON2, .con3 = ETDM_OUT1_CON3,
		.con4 = ETDM_OUT1_CON4, .con5 = ETDM_OUT1_CON5,
	},
	[MT8188_ETDM_OUT2] = {
		.con0 = ETDM_OUT2_CON0, .con1 = ETDM_OUT2_CON1,
		.con2 = ETDM_OUT2_CON2, .con3 = ETDM_OUT2_CON3,
		.con4 = ETDM_OUT2_CON4, .con5 = ETDM_OUT2_CON5,
	},
	[MT8188_ETDM_IN1] = {
		.con0 = ETDM_IN1_CON0, .con1 = ETDM_IN1_CON1,
		.con2 = ETDM_IN1_CON2, .con3 = ETDM_IN1_CON3,
		.con4 = ETDM_IN1_CON4, .con5 = ETDM_IN1_CON5,
	},
	[MT8188_ETDM_IN2] = {
		.con0 = ETDM_IN2_CON0, .con1 = ETDM_IN2_CON1,
		.con2 = ETDM_IN2_CON2, .con3 = ETDM_IN2_CON3,
		.con4 = ETDM_IN2_CON4, .con5 = ETDM_IN2_CON5,
	},
};
/* clang-format on */

/* -------------------------------------------------------------------------
 * get_etdm_fs_timing — rate → eTDM CON3/CON4 FS field value
 * Source: mt8188_etdm_rates[] in Linux mt8188-dai-etdm.c
 * Used for: ETDM_IN_CON3_FS (IN paths), ETDM_OUT_CON4_FS (OUT paths)
 * NOTE: different encoding from mt8188_afe_rates (used for AFIFO + memif)
 * -------------------------------------------------------------------------
 */
static int get_etdm_fs_timing(uint32_t rate)
{
	switch (rate) {
	case 8000:
		return 0;
	case 12000:
		return 1;
	case 16000:
		return 2;
	case 24000:
		return 3;
	case 32000:
		return 4;
	case 48000:
		return 5;
	case 96000:
		return 7;
	case 192000:
		return 9;
	case 384000:
		return 11;
	case 11025:
		return 16;
	case 22050:
		return 17;
	case 44100:
		return 18;
	case 88200:
		return 19;
	case 176400:
		return 20;
	case 352800:
		return 21;
	default:
		return -EINVAL;
	}
}

/* -------------------------------------------------------------------------
 * get_afifo_fs_timing — rate → AFIFO mode field value
 * Source: mt8188_afe_rates[] in Linux mt8188-afe-pcm.c
 * Used for: ETDM_IN*_AFIFO_CON mode field (and memif FS — different table)
 * -------------------------------------------------------------------------
 */
static int get_afifo_fs_timing(uint32_t rate)
{
	switch (rate) {
	case 8000:
		return 0;
	case 12000:
		return 1;
	case 16000:
		return 2;
	case 24000:
		return 3;
	case 32000:
		return 4;
	case 48000:
		return 5;
	case 96000:
		return 6;
	case 192000:
		return 7;
	case 384000:
		return 8;
	case 7350:
		return 16;
	case 11025:
		return 17;
	case 14700:
		return 18;
	case 22050:
		return 19;
	case 29400:
		return 20;
	case 44100:
		return 21;
	case 88200:
		return 22;
	case 176400:
		return 23;
	case 352800:
		return 24;
	default:
		return -EINVAL;
	}
}

/* -------------------------------------------------------------------------
 * get_etdm_wlen — bit width → word length register value (16 or 32)
 * Linux: get_etdm_wlen()
 * -------------------------------------------------------------------------
 */
static uint32_t get_etdm_wlen(uint32_t bit_width)
{
	return bit_width <= 16 ? 16 : 32;
}

/* -------------------------------------------------------------------------
 * get_etdm_ch_fixup — round up channel count to 2/4/8/16
 * Linux: get_etdm_ch_fixup()
 * -------------------------------------------------------------------------
 */
static uint32_t get_etdm_ch_fixup(uint32_t channels)
{
	if (channels > 8) {
		return 16;
	} else if (channels > 4) {
		return 8;
	} else if (channels > 2) {
		return 4;
	} else {
		return 2;
	}
}

/* -------------------------------------------------------------------------
 * etdm_cowork_slv_sel — DAI id + slave_mode → COWORK slave-select value
 * Linux: etdm_cowork_slv_sel()
 * -------------------------------------------------------------------------
 */
static int etdm_cowork_slv_sel(enum mt8188_etdm_id id, bool slave_mode)
{
	if (slave_mode) {
		switch (id) {
		case MT8188_ETDM_IN1:
			return MT8188_COWORK_ETDM_IN1_S;
		case MT8188_ETDM_IN2:
			return MT8188_COWORK_ETDM_IN2_S;
		case MT8188_ETDM_OUT1:
			return MT8188_COWORK_ETDM_OUT1_S;
		case MT8188_ETDM_OUT2:
			return MT8188_COWORK_ETDM_OUT2_S;
		default:
			return -EINVAL;
		}
	} else {
		switch (id) {
		case MT8188_ETDM_IN1:
			return MT8188_COWORK_ETDM_IN1_M;
		case MT8188_ETDM_IN2:
			return MT8188_COWORK_ETDM_IN2_M;
		case MT8188_ETDM_OUT1:
			return MT8188_COWORK_ETDM_OUT1_M;
		case MT8188_ETDM_OUT2:
			return MT8188_COWORK_ETDM_OUT2_M;
		default:
			return -EINVAL;
		}
	}
}

/* -------------------------------------------------------------------------
 * etdm_cowork_sync_sel — DAI id → COWORK sync-select value
 * Linux: etdm_cowork_sync_sel()
 * -------------------------------------------------------------------------
 */
static int etdm_cowork_sync_sel(enum mt8188_etdm_id id)
{
	switch (id) {
	case MT8188_ETDM_IN1:
		return MT8188_ETDM_SYNC_FROM_IN1;
	case MT8188_ETDM_IN2:
		return MT8188_ETDM_SYNC_FROM_IN2;
	case MT8188_ETDM_OUT1:
		return MT8188_ETDM_SYNC_FROM_OUT1;
	case MT8188_ETDM_OUT2:
		return MT8188_ETDM_SYNC_FROM_OUT2;
	default:
		return -EINVAL;
	}
}

/* -------------------------------------------------------------------------
 * AFIFO setup for IN paths
 * Linux: mtk_dai_etdm_fifo_mode()
 * slave_mode → rate=0 (uses 1X_EN token), master → rate (uses FS timing)
 * -------------------------------------------------------------------------
 */
static int etdm_fifo_mode(uintptr_t base, enum mt8188_etdm_id id, uint32_t rate)
{
	uint32_t reg;
	uint32_t mode;
	uint32_t one_x;
	uint32_t mask = ETDM_IN_AFIFO_MODE_MASK | ETDM_IN_USE_AFIFO;

	switch (id) {
	case MT8188_ETDM_IN1:
		reg = ETDM_IN1_AFIFO_CON;
		one_x = MT8188_ETDM_IN1_1X_EN;
		break;
	case MT8188_ETDM_IN2:
		reg = ETDM_IN2_AFIFO_CON;
		one_x = MT8188_ETDM_IN2_1X_EN;
		break;
	default:
		return -EINVAL;
	}

	if (rate == 0U) {
		/* Slave: the FIFO follows the port's own recovered clock. */
		mode = one_x;
	} else {
		/*
		 * Master: report an unsupported rate rather than casting the
		 * error through.  A negative timing value cast to unsigned lands
		 * in the mode field as a nonsense selector the block accepts.
		 */
		int fs = get_afifo_fs_timing(rate);

		if (fs < 0) {
			return fs;
		}
		mode = (uint32_t)fs;
	}

	reg_update_bits(base, reg, mask, mode | ETDM_IN_USE_AFIFO);
	return 0;
}

/* -------------------------------------------------------------------------
 * eTDM MCLK source selection — write CLOCK field in CON2 (IN) or CON4 (OUT)
 * Linux: mt8188_etdm_clk_src_sel_put() (mt8188_etdm_clk_src_sel_text[])
 *
 * source values (3-bit field):
 *   0 = 26m
 *   1 = a1sys/a2sys   (driver default for all eTDM ports)
 *   2 = a3sys
 *   3 = a4sys
 * -------------------------------------------------------------------------
 */
static void etdm_set_clock_source(uintptr_t base, enum mt8188_etdm_id id, uint32_t source)
{
	uint32_t reg, mask;

	if (id == MT8188_ETDM_IN1 || id == MT8188_ETDM_IN2) {
		reg = etdm_reg_table[id].con2;
		mask = ETDM_IN_CON2_CLOCK_MASK;
		reg_update_bits(base, reg, mask, FIELD_PREP(ETDM_IN_CON2_CLOCK_MASK, source));
	} else {
		reg = etdm_reg_table[id].con4;
		mask = ETDM_OUT_CON4_CLOCK_MASK;
		reg_update_bits(base, reg, mask, FIELD_PREP(ETDM_OUT_CON4_CLOCK_MASK, source));
	}
}

/* -------------------------------------------------------------------------
 * IN-specific CON1..CON5 configuration
 * Linux: mtk_dai_etdm_in_configure()
 * -------------------------------------------------------------------------
 */
static int etdm_in_configure(uintptr_t base, enum mt8188_etdm_id id, uint32_t rate,
			     uint32_t channels, const struct mt8188_etdm_config *ecfg)
{
	const struct etdm_regs *r = &etdm_reg_table[id];
	uint32_t mask, val;
	int i;

	/* AFIFO */
	etdm_fifo_mode(base, id, ecfg->slave_mode ? 0 : rate);

	/* CON1: LRCK width */
	mask = 0;
	val = 0;
	if (ecfg->lrck_width > 0) {
		mask |= ETDM_IN_CON1_LRCK_AUTO_MODE | ETDM_IN_CON1_LRCK_WIDTH_MASK;
		val |= FIELD_PREP(ETDM_IN_CON1_LRCK_WIDTH_MASK, ecfg->lrck_width - 1);
	}
	reg_update_bits(base, r->con1, mask, val);

	/* CON2: update-gap + multi-IP channel count */
	mask = 0;
	val = 0;
	if (!ecfg->slave_mode) {
		mask |= ETDM_IN_CON2_UPDATE_GAP_MASK;
		val |= FIELD_PREP(ETDM_IN_CON2_UPDATE_GAP_MASK,
				  (rate == 352800 || rate == 384000) ? 4 : 3);
	}
	mask |= ETDM_IN_CON2_MULTI_IP_2CH_MODE | ETDM_IN_CON2_MULTI_IP_TOTAL_CH_MASK;
	if (ecfg->data_mode == MT8188_ETDM_DATA_MULTI_PIN) {
		val |= ETDM_IN_CON2_MULTI_IP_2CH_MODE |
		       FIELD_PREP(ETDM_IN_CON2_MULTI_IP_TOTAL_CH_MASK, channels - 1);
	}
	reg_update_bits(base, r->con2, mask, val);

	/* CON3: per-channel-pair disable + FS */
	mask = ETDM_IN_CON3_DISABLE_OUT_MASK;
	val = 0;
	for (i = 0; i < (int)channels && i < MT8188_ETDM_MAX_CHANNELS - 1; i += 2) {
		if (ecfg->in_disable_ch[i] && ecfg->in_disable_ch[i + 1]) {
			val |= ETDM_IN_CON3_DISABLE_OUT(i >> 1);
		}
	}
	if (!ecfg->slave_mode) {
		int fs = get_etdm_fs_timing(rate);

		if (fs < 0) {
			return fs;
		}
		mask |= ETDM_IN_CON3_FS_MASK;
		val |= FIELD_PREP(ETDM_IN_CON3_FS_MASK, (uint32_t)fs);
	}
	reg_update_bits(base, r->con3, mask, val);

	/* CON4: LRCK/BCK inversion */
	mask = ETDM_IN_CON4_MASTER_LRCK_INV | ETDM_IN_CON4_MASTER_BCK_INV |
	       ETDM_IN_CON4_SLAVE_LRCK_INV | ETDM_IN_CON4_SLAVE_BCK_INV;
	val = 0;
	if (ecfg->slave_mode) {
		if (ecfg->lrck_inv) {
			val |= ETDM_IN_CON4_SLAVE_LRCK_INV;
		}
		if (ecfg->bck_inv) {
			val |= ETDM_IN_CON4_SLAVE_BCK_INV;
		}
	} else {
		if (ecfg->lrck_inv) {
			val |= ETDM_IN_CON4_MASTER_LRCK_INV;
		}
		if (ecfg->bck_inv) {
			val |= ETDM_IN_CON4_MASTER_BCK_INV;
		}
	}
	reg_update_bits(base, r->con4, mask, val);

	/* CON5: L/R swap and odd-channel enable (per-channel-pair disable logic) */
	mask = ETDM_IN_CON5_LR_SWAP_MASK | ETDM_IN_CON5_ENABLE_ODD_MASK;
	val = 0;
	for (i = 0; i < (int)channels && i < MT8188_ETDM_MAX_CHANNELS - 1; i += 2) {
		if (ecfg->in_disable_ch[i] && !ecfg->in_disable_ch[i + 1]) {
			val |= ETDM_IN_CON5_LR_SWAP(i >> 1);
			val |= ETDM_IN_CON5_ENABLE_ODD(i >> 1);
		} else if (!ecfg->in_disable_ch[i] && ecfg->in_disable_ch[i + 1]) {
			val |= ETDM_IN_CON5_ENABLE_ODD(i >> 1);
		}
	}
	reg_update_bits(base, r->con5, mask, val);

	return 0;
}

/* -------------------------------------------------------------------------
 * OUT-specific CON0/CON1/CON4/CON5 configuration
 * Linux: mtk_dai_etdm_out_configure()
 * -------------------------------------------------------------------------
 */
static int etdm_out_configure(uintptr_t base, enum mt8188_etdm_id id, uint32_t rate,
			      uint32_t channels, const struct mt8188_etdm_config *ecfg)
{
	const struct etdm_regs *r = &etdm_reg_table[id];
	uint32_t mask, val;
	int fs;

	/* CON0: relatch domain = A1A2SYS */
	reg_update_bits(base, r->con0, ETDM_OUT_CON0_RELATCH_DOMAIN_MASK,
			FIELD_PREP(ETDM_OUT_CON0_RELATCH_DOMAIN_MASK, 0));

	/* CON1: LRCK width */
	mask = 0;
	val = 0;
	if (ecfg->lrck_width > 0) {
		mask |= ETDM_OUT_CON1_LRCK_AUTO_MODE | ETDM_OUT_CON1_LRCK_WIDTH_MASK;
		val |= FIELD_PREP(ETDM_OUT_CON1_LRCK_WIDTH_MASK, ecfg->lrck_width - 1);
	}
	reg_update_bits(base, r->con1, mask, val);

	/* CON4: FS + relatch enable */
	mask = ETDM_OUT_CON4_RELATCH_EN_MASK;
	val = FIELD_PREP(ETDM_OUT_CON4_RELATCH_EN_MASK, (id == MT8188_ETDM_OUT1)
								? MT8188_ETDM_OUT1_1X_EN
								: MT8188_ETDM_OUT2_1X_EN);
	if (!ecfg->slave_mode) {
		fs = get_etdm_fs_timing(rate);
		if (fs < 0) {
			return fs;
		}
		mask |= ETDM_OUT_CON4_FS_MASK;
		val |= FIELD_PREP(ETDM_OUT_CON4_FS_MASK, (uint32_t)fs);
	}
	reg_update_bits(base, r->con4, mask, val);

	/* CON5: LRCK/BCK inversion */
	mask = ETDM_OUT_CON5_MASTER_LRCK_INV | ETDM_OUT_CON5_MASTER_BCK_INV |
	       ETDM_OUT_CON5_SLAVE_LRCK_INV | ETDM_OUT_CON5_SLAVE_BCK_INV;
	val = 0;
	if (ecfg->slave_mode) {
		if (ecfg->lrck_inv) {
			val |= ETDM_OUT_CON5_SLAVE_LRCK_INV;
		}
		if (ecfg->bck_inv) {
			val |= ETDM_OUT_CON5_SLAVE_BCK_INV;
		}
	} else {
		if (ecfg->lrck_inv) {
			val |= ETDM_OUT_CON5_MASTER_LRCK_INV;
		}
		if (ecfg->bck_inv) {
			val |= ETDM_OUT_CON5_MASTER_BCK_INV;
		}
	}
	reg_update_bits(base, r->con5, mask, val);

	return 0;
}

/* -------------------------------------------------------------------------
 * Cowork sync-mode: slave path
 * Linux: mt8188_etdm_sync_mode_slv()
 * -------------------------------------------------------------------------
 */
static int etdm_sync_mode_slv(uintptr_t base, enum mt8188_etdm_id id, int cowork_source_id,
			      bool source_is_slave)
{
	uint32_t reg, mask, val;
	/*
	 * The SLAVE_SEL field names which clock inside the source port to tap,
	 * so the _M / _S suffix describes the SOURCE port's role, not this
	 * port's: _M is the clock the source generates as a master, _S is the
	 * clock the source itself receives on its pins. Compare
	 * MT8188_ETDM_SYNC_FROM_OUT1 (10) == MT8188_COWORK_ETDM_OUT1_M — to
	 * sync from a master port you write its _M value.
	 *
	 * Hardcoding true here made a port slaved to a master (DL11 32ch:
	 * eTDM_OUT2 slaved to the eTDM_OUT1 master) select OUT1_S, a clock
	 * that is never generated, so the slave port emitted no data at all.
	 */
	int cowork_source_sel =
		etdm_cowork_slv_sel((enum mt8188_etdm_id)cowork_source_id, source_is_slave);

	if (cowork_source_sel < 0) {
		return cowork_source_sel;
	}

	switch (id) {
	case MT8188_ETDM_IN1:
		reg = ETDM_COWORK_CON1;
		mask = ETDM_IN1_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_IN1_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_ETDM_IN2:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_IN2_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_IN2_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_ETDM_OUT1:
		reg = ETDM_COWORK_CON0;
		mask = ETDM_OUT1_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT1_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	case MT8188_ETDM_OUT2:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_OUT2_SLAVE_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT2_SLAVE_SEL_MASK, cowork_source_sel);
		break;
	default:
		return 0;
	}

	reg_update_bits(base, reg, mask, val);
	return 0;
}

/* -------------------------------------------------------------------------
 * Cowork sync-mode: master path
 * Linux: mt8188_etdm_sync_mode_mst()
 * -------------------------------------------------------------------------
 */
static int etdm_sync_mode_mst(uintptr_t base, enum mt8188_etdm_id id, int cowork_source_id)
{
	uint32_t reg, mask, val;
	/*
	 * Linux mt8188_etdm_sync_mode_mst():
	 *   cowork_source_sel = etdm_cowork_sync_sel(etdm_data->cowork_source_id)
	 *
	 * cowork_source_id is already an etdm_id — pass directly, no conversion.
	 */
	int sync_sel = etdm_cowork_sync_sel((enum mt8188_etdm_id)cowork_source_id);

	if (sync_sel < 0) {
		return sync_sel;
	}

	switch (id) {
	case MT8188_ETDM_IN1:
		reg = ETDM_COWORK_CON1;
		mask = ETDM_IN1_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_IN1_SYNC_SEL_MASK, sync_sel);
		break;
	case MT8188_ETDM_IN2:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_IN2_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_IN2_SYNC_SEL_MASK, sync_sel);
		break;
	case MT8188_ETDM_OUT1:
		reg = ETDM_COWORK_CON0;
		mask = ETDM_OUT1_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT1_SYNC_SEL_MASK, sync_sel);
		break;
	case MT8188_ETDM_OUT2:
		reg = ETDM_COWORK_CON2;
		mask = ETDM_OUT2_SYNC_SEL_MASK;
		val = FIELD_PREP(ETDM_OUT2_SYNC_SEL_MASK, sync_sel);
		break;
	default:
		return 0;
	}

	reg_update_bits(base, reg, mask, val);
	/* Set SYNC_MODE bit in this port's CON0 */
	reg_set_bits(base, etdm_reg_table[id].con0, ETDM_CON0_SYNC_MODE);
	return 0;
}

/* -------------------------------------------------------------------------
 * Cowork sync dispatcher
 * Linux: mt8188_etdm_sync_mode_configure()
 * -------------------------------------------------------------------------
 */
static int etdm_sync_mode_configure(struct mt8188_afe *afe, enum mt8188_etdm_id id,
				    const struct mt8188_etdm_config *ecfg)
{
	int src = ecfg->cowork_source_id;

	if (src == MT8188_COWORK_SOURCE_NONE) {
		/*
		 * A port with no cowork source must not be left in sync mode:
		 * it would wait for a frame sync from a source that is not
		 * running, so BCK is generated but LRCK and data stay low.
		 * eTDM_OUT2 comes out of reset with SYNC_MODE already set and
		 * nothing else clears it, which left DL8 playback silent.
		 */
		reg_update_bits(afe->base, etdm_reg_table[id].con0, ETDM_CON0_SYNC_MODE, 0);

		if (ecfg->slave_mode) {
			/*
			 * External slave: BCK/LRCK come from this port's own
			 * pins, so SLAVE_SEL has to name the port's own _S
			 * clock. It is honoured even with SYNC_MODE clear, and
			 * eTDM_IN2 comes up selecting eTDM_IN1_S, a clock that
			 * only exists while eTDM_IN1 runs — which left UL3
			 * capture unclocked whenever eTDM_IN1 was idle.
			 */
			return etdm_sync_mode_slv(afe->base, id, id, true);
		}
		return 0;
	}
	if (src < 0 || src >= MT8188_ETDM_NR) {
		return -EINVAL;
	}

	if (ecfg->slave_mode) {
		/* The selector identifies the source's clock, so it depends on
		 * whether the source drives its own clocks or receives them.
		 */
		return etdm_sync_mode_slv(afe->base, id, src, afe->etdm[src].slave_mode);
	} else {
		return etdm_sync_mode_mst(afe->base, id, src);
	}
}

/* =========================================================================
 * MCLK / APLL tuner programming
 * Ported from Linux mt8188-afe-clk.c and mt8188-dai-etdm.c.
 *
 * Linux uses CCF (clk_set_rate / clk_set_parent / clk_prepare_enable) which
 * programs TOPCKGEN divider registers and enables divider gates via the CCF.
 * In Zephyr we do the same operations directly:
 *   - Divider ratio   → write TOPCKGEN APLL12_DIV* register (TOPCKGEN base)
 *   - Divider gate    → clock_control_on(clk_topckgen, CLK_TOP_APLL12_CK_DIVx)
 *   - Mux parent      → already set by clock_control_on(clk_topckgen, I2SIx/I2SOx)
 *   - APLL tuner      → direct MMIO to AFE_APLL_TUNER_CFG/CFG1 on AFE base
 *   - A1SYS/A2SYS     → AUDSYS gate + ASYS_TOP_CON timing bit (already in afe-clk.c)
 * =========================================================================
 */

/*
 * TOPCKGEN base address — needed to write divider ratio registers directly.
 * The topckgen device already maps this in its own MMIO region; here we
 * access it via DEVICE_MMIO_GET on the topckgen device.
 */

/*
 * APLL source rates (fixed):
 *   APLL1 = 196,608,000 Hz  (48kHz family)
 *   APLL2 = 180,633,600 Hz  (44.1kHz family)
 */
#define MT8188_APLL1_RATE 196608000U
#define MT8188_APLL2_RATE 180633600U

/*
 * TOPCKGEN APLL12_DIV register layout (from Linux clk-mt8188-topckgen.c):
 *   DIV_GATE(CLK_TOP_APLL12_CK_DIV0, ..., gate@0x320[0], div@0x0328, width=8, shift=0)
 *   DIV_GATE(CLK_TOP_APLL12_CK_DIV1, ..., gate@0x320[1], div@0x0328, width=8, shift=8)
 *   DIV_GATE(CLK_TOP_APLL12_CK_DIV2, ..., gate@0x320[2], div@0x0328, width=8, shift=16)
 *   DIV_GATE(CLK_TOP_APLL12_CK_DIV3, ..., gate@0x320[3], div@0x0328, width=8, shift=24)
 *
 * Register offset 0x0328 within TOPCKGEN base; value = (apll_rate / mclk_freq) - 1
 */
#define TOPCK_APLL12_DIV_REG  0x0328U
#define TOPCK_APLL12_DIV_MASK 0xFFU

/*
 * Map etdm_id → APLL12_CK_DIV CLK_TOP_* gate ID and byte shift in 0x0328
 * Linux: mtk_dai_etdm_get_clkdiv_id_by_dai_id()
 *   IN1  → CLK_TOP_APLL12_CK_DIV0, shift 0
 *   IN2  → CLK_TOP_APLL12_CK_DIV1, shift 8
 *   OUT1 → CLK_TOP_APLL12_CK_DIV2, shift 16
 *   OUT2 → CLK_TOP_APLL12_CK_DIV3, shift 24
 */
struct apll12_div_info {
	uint32_t clk_id;   /* CLK_TOP_APLL12_CK_DIVx (divider gate) */
	uint8_t div_shift; /* bit shift in TOPCK_APLL12_DIV_REG */
	uint32_t mux_id;   /* CLK_TOP_I2SIx/I2SOx (per-port MCLK mux) */
};

static const struct apll12_div_info apll12_div_table[MT8188_ETDM_NR] = {
	[MT8188_ETDM_IN1] = {CLK_TOP_APLL12_CK_DIV0, 0, CLK_TOP_I2SI1},
	[MT8188_ETDM_IN2] = {CLK_TOP_APLL12_CK_DIV1, 8, CLK_TOP_I2SI2},
	[MT8188_ETDM_OUT1] = {CLK_TOP_APLL12_CK_DIV2, 16, CLK_TOP_I2SO1},
	[MT8188_ETDM_OUT2] = {CLK_TOP_APLL12_CK_DIV3, 24, CLK_TOP_I2SO2},
};

/*
 * MCLK mux parent selector values (i2si1/i2so1/... parent list, from
 * Linux clk-mt8188-topckgen.c): { clk26m, apll1, apll2, apll3, apll4, apll5 }.
 *   apll_idx 0 (APLL1) → parent 1
 *   apll_idx 1 (APLL2) → parent 2
 */
#define MT8188_MCLK_MUX_PARENT_APLL1 1U
#define MT8188_MCLK_MUX_PARENT_APLL2 2U

/*
 * APLL tuner configuration — direct MMIO on AFE base.
 * Source: mt8188_afe_tuner_cfgs[] in Linux mt8188-afe-clk.c
 * Only APLL1 (PLL1) and APLL2 (PLL2) are used for audio eTDM.
 */
struct apll_tuner_cfg {
	uint32_t apll_div_reg;
	uint8_t apll_div_shift;
	uint8_t apll_div_maskbit;
	uint8_t apll_div_default;
	uint32_t ref_ck_sel_reg;
	uint8_t ref_ck_sel_shift;
	uint8_t ref_ck_sel_maskbit;
	uint8_t ref_ck_sel_default;
	uint32_t tuner_en_reg;
	uint8_t tuner_en_shift;
	uint32_t upper_bound_reg;
	uint8_t upper_bound_shift;
	uint8_t upper_bound_maskbit;
	uint8_t upper_bound_default;
};

/* clang-format off */
static const struct apll_tuner_cfg apll_tuner_cfgs[2] = {
	/* APLL1 */
	[0] = {
		.apll_div_reg      = AFE_APLL_TUNER_CFG,
		.apll_div_shift    = 4,
		.apll_div_maskbit  = 0xf,
		.apll_div_default  = 0x7,
		.ref_ck_sel_reg    = AFE_APLL_TUNER_CFG,
		.ref_ck_sel_shift  = 1,
		.ref_ck_sel_maskbit = 0x3,
		.ref_ck_sel_default = 0x2,
		.tuner_en_reg      = AFE_APLL_TUNER_CFG,
		.tuner_en_shift    = 0,
		.upper_bound_reg   = AFE_APLL_TUNER_CFG,
		.upper_bound_shift = 8,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x3,
	},
	/* APLL2 */
	[1] = {
		.apll_div_reg      = AFE_APLL_TUNER_CFG1,
		.apll_div_shift    = 4,
		.apll_div_maskbit  = 0xf,
		.apll_div_default  = 0x7,
		.ref_ck_sel_reg    = AFE_APLL_TUNER_CFG1,
		.ref_ck_sel_shift  = 1,
		.ref_ck_sel_maskbit = 0x3,
		.ref_ck_sel_default = 0x1,
		.tuner_en_reg      = AFE_APLL_TUNER_CFG1,
		.tuner_en_shift    = 0,
		.upper_bound_reg   = AFE_APLL_TUNER_CFG1,
		.upper_bound_shift = 8,
		.upper_bound_maskbit = 0xff,
		.upper_bound_default = 0x3,
	},
};
/* clang-format on */

/*
 * setup_apll_tuner — write default divider, ref-ck-sel, and upper-bound.
 * Linux: mt8188_afe_setup_apll_tuner()
 */
static void setup_apll_tuner(uintptr_t afe_base, int apll_idx)
{
	const struct apll_tuner_cfg *c = &apll_tuner_cfgs[apll_idx];

	reg_update_bits(afe_base, c->apll_div_reg,
			(uint32_t)c->apll_div_maskbit << c->apll_div_shift,
			(uint32_t)c->apll_div_default << c->apll_div_shift);
	reg_update_bits(afe_base, c->ref_ck_sel_reg,
			(uint32_t)c->ref_ck_sel_maskbit << c->ref_ck_sel_shift,
			(uint32_t)c->ref_ck_sel_default << c->ref_ck_sel_shift);
	reg_update_bits(afe_base, c->upper_bound_reg,
			(uint32_t)c->upper_bound_maskbit << c->upper_bound_shift,
			(uint32_t)c->upper_bound_default << c->upper_bound_shift);
}

/*
 * enable_apll_tuner / disable_apll_tuner
 * Linux: mt8188_afe_enable_apll_tuner() / mt8188_afe_disable_apll_tuner()
 * Tuner gate clocks (aud_apll, aud_apll1_tuner, etc.) are AUDSYS gates
 * already managed by mt8188-audsys-clk.c — handled in mt8188-afe-clk.c.
 * Here we only manage the tuner enable bit in the AFE register.
 */
static void enable_apll_tuner(uintptr_t afe_base, int apll_idx)
{
	const struct apll_tuner_cfg *c = &apll_tuner_cfgs[apll_idx];

	setup_apll_tuner(afe_base, apll_idx);
	reg_set_bits(afe_base, c->tuner_en_reg, BIT(c->tuner_en_shift));
}

static void disable_apll_tuner(uintptr_t afe_base, int apll_idx)
{
	const struct apll_tuner_cfg *c = &apll_tuner_cfgs[apll_idx];

	reg_update_bits(afe_base, c->tuner_en_reg, BIT(c->tuner_en_shift), 0);
}

/*
 * get_apll_rate — return the fixed APLL output frequency
 */
static uint32_t get_apll_rate(int apll_idx)
{
	return (apll_idx == 0) ? MT8188_APLL1_RATE : MT8188_APLL2_RATE;
}

/*
 * topckgen_base — retrieve TOPCKGEN MMIO base from the topckgen device
 */
static uintptr_t topckgen_base(const struct mt8188_afe *afe)
{
	return DEVICE_MMIO_GET(afe->clk_topckgen);
}

/*
 * mt8188_etdm_cal_mclk — validate mclk_freq against APLL and store.
 * Linux: mtk_dai_etdm_cal_mclk()
 */
int mt8188_etdm_cal_mclk(struct mt8188_afe *afe, enum mt8188_etdm_id id, uint32_t freq)
{
	struct mt8188_etdm_config *ecfg;
	int apll_idx;
	uint32_t apll_rate;

	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	ecfg = &afe->etdm[id];

	if (freq == 0) {
		ecfg->mclk_freq = 0;
		ecfg->mclk_apll_valid = false;
		return 0;
	}

	/*
	 * Pick the PLL from this port's sample rate, not from the master clock
	 * frequency.  The two agree for the usual frequencies, because the
	 * 48 kHz family's are multiples of 8000 and the 44.1 kHz family's are
	 * not, but the rate is what actually decides the family and an unusual
	 * master clock would otherwise select the wrong PLL.
	 *
	 * Recomputed on every call rather than latched: a port moved from a
	 * 48 kHz stream to a 44.1 kHz one has to move PLL with it, and keeping
	 * the first choice left it dividing the new rate out of the old PLL.
	 */
	apll_idx = mt8188_afe_apll_by_rate(ecfg->rate != 0U ? ecfg->rate : freq);
	apll_rate = get_apll_rate(apll_idx);

	if (freq > apll_rate) {
		return -EINVAL;
	}
	if (apll_rate % freq != 0) {
		return -EINVAL;
	}

	/*
	 * The divider is an 8-bit field holding the ratio less one, so a ratio
	 * the PLL divides exactly can still be too large to program.  Rejecting
	 * it here is better than truncating it into a master clock that is a
	 * multiple of the one asked for.
	 */
	if ((apll_rate / freq) > 256U) {
		return -EINVAL;
	}

	ecfg->mclk_apll = (uint32_t)apll_idx;
	ecfg->mclk_apll_valid = true;
	ecfg->mclk_freq = freq;
	return 0;
}

/*
 * mt8188_etdm_enable_mclk — program divider, enable gate, set tuner.
 * Linux: mtk_dai_etdm_enable_mclk()
 *
 * Steps (matching Linux sequence):
 *  1. Set ETDM_CON1_MCLK_OUTPUT flag if mclk_dir == OUT
 *  2. Enable the mux clock (CLK_TOP_I2SIx / I2SOx) — already done by
 *     afe-clk.c when enabling the eTDM path clocks, so skip here.
 *  3. Write divider ratio to TOPCKGEN APLL12_DIV register
 *  4. Enable divider gate via clock_control_on(topckgen, CLK_TOP_APLL12_CK_DIVx)
 *  5. Set up and enable APLL tuner
 */
int mt8188_etdm_enable_mclk(struct mt8188_afe *afe, enum mt8188_etdm_id id)
{
	const struct mt8188_etdm_config *ecfg;
	const struct apll12_div_info *div;
	const struct etdm_regs *r;
	uintptr_t topck;
	uint32_t apll_rate, div_val;
	int apll_idx;
	int ret;

	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	ecfg = &afe->etdm[id];

	if (ecfg->mclk_freq == 0) {
		/* No MCLK to generate. A port asked to take MCLK in must not
		 * drive its MCK pin: on the eTDM_IN ports that pin is
		 * bidirectional, so leaving CON1 at its reset value is not
		 * enough once an external master is wired to it.
		 */
		if (ecfg->mclk_dir == MT8188_MCLK_DIR_IN) {
			reg_update_bits(afe->base, etdm_reg_table[id].con1, ETDM_CON1_MCLK_OUTPUT,
					0);
		}
		return 0;
	}

	div = &apll12_div_table[id];
	r = &etdm_reg_table[id];
	apll_idx =
		ecfg->mclk_apll_valid ? (int)ecfg->mclk_apll : mt8188_afe_apll_by_rate(ecfg->rate);
	apll_rate = get_apll_rate(apll_idx);
	topck = topckgen_base(afe);

	/* Step 1: MCLK output direction flag in CON1 */
	reg_update_bits(afe->base, r->con1, ETDM_CON1_MCLK_OUTPUT,
			(ecfg->mclk_dir != 0) ? ETDM_CON1_MCLK_OUTPUT : 0);

	/* Step 2: Reparent the per-port MCLK mux (I2SIx/I2SOx) to the APLL
	 * chosen by cal_mclk — Linux mt8188_afe_set_clk_parent(). The mux and
	 * the divider must agree on the APLL, or a 44.1k stream (APLL2) would
	 * be divided from APLL1 and emit the wrong MCLK.
	 */
	{
		uint8_t parent = (apll_idx == 0) ? MT8188_MCLK_MUX_PARENT_APLL1
						 : MT8188_MCLK_MUX_PARENT_APLL2;

		ret = clock_control_configure(
			afe->clk_topckgen, (clock_control_subsys_t)(uintptr_t)div->mux_id, &parent);
		if (ret) {
			return ret;
		}
	}

	/* Step 3: Write divider ratio — (apll_rate / mclk_freq) - 1 */
	div_val = (apll_rate / ecfg->mclk_freq) - 1;
	reg_update_bits(topck, TOPCK_APLL12_DIV_REG, TOPCK_APLL12_DIV_MASK << div->div_shift,
			(div_val & TOPCK_APLL12_DIV_MASK) << div->div_shift);

	/* Step 4: Enable divider gate */
	ret = clock_control_on(afe->clk_topckgen, (clock_control_subsys_t)(uintptr_t)div->clk_id);
	if (ret) {
		return ret;
	}

	/* Step 5: Enable APLL tuner */
	enable_apll_tuner(afe->base, apll_idx);

	return 0;
}

/*
 * mt8188_etdm_disable_mclk — disable gate and tuner.
 * Linux: mtk_dai_etdm_disable_mclk()
 */
int mt8188_etdm_disable_mclk(struct mt8188_afe *afe, enum mt8188_etdm_id id)
{
	const struct mt8188_etdm_config *ecfg;
	const struct apll12_div_info *div;
	int apll_idx;

	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	ecfg = &afe->etdm[id];

	if (ecfg->mclk_freq == 0) {
		return 0;
	}

	div = &apll12_div_table[id];
	apll_idx =
		ecfg->mclk_apll_valid ? (int)ecfg->mclk_apll : mt8188_afe_apll_by_rate(ecfg->rate);

	/* Disable divider gate */
	clock_control_off(afe->clk_topckgen, (clock_control_subsys_t)(uintptr_t)div->clk_id);

	/* Disable APLL tuner */
	disable_apll_tuner(afe->base, apll_idx);

	return 0;
}

/* =========================================================================
 * Public API
 * =========================================================================
 */

/* -------------------------------------------------------------------------
 * mt8188_etdm_set_fmt — store format, inversion, master/slave in priv
 * Call before mt8188_etdm_configure().
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_set_fmt(struct mt8188_afe *afe, enum mt8188_etdm_id id, enum mt8188_etdm_fmt fmt,
			bool lrck_inv, bool bck_inv, bool slave_mode)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	/* ETDM_OUT1 is master-only (same constraint as Linux) */
	if (slave_mode && id == MT8188_ETDM_OUT1) {
		return -EINVAL;
	}
	afe->etdm[id].fmt = fmt;
	afe->etdm[id].lrck_inv = lrck_inv;
	afe->etdm[id].bck_inv = bck_inv;
	afe->etdm[id].slave_mode = slave_mode;
	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_set_tdm_slot — store slot count + LRCK width override
 * Call before mt8188_etdm_configure().
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_set_tdm_slot(struct mt8188_afe *afe, enum mt8188_etdm_id id, uint32_t slots,
			     uint32_t lrck_width)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	afe->etdm[id].slots = slots;
	afe->etdm[id].lrck_width = lrck_width;
	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_set_sysclk — store direction and validate/store mclk_freq.
 * Linux: mtk_dai_etdm_set_sysclk() — stores mclk_dir then calls cal_mclk.
 * Call before mt8188_etdm_enable_mclk().
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_set_sysclk(struct mt8188_afe *afe, enum mt8188_etdm_id id, uint32_t mclk_freq,
			   int mclk_dir)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	afe->etdm[id].mclk_dir = mclk_dir;
	return mt8188_etdm_cal_mclk(afe, id, mclk_freq);
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_set_data_mode — single-pin vs multi-pin
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_set_data_mode(struct mt8188_afe *afe, enum mt8188_etdm_id id,
			      enum mt8188_etdm_data_mode mode)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	afe->etdm[id].data_mode = mode;
	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_set_cowork_source — set cowork master DAI for this port
 *
 * @cowork_source_id: enum mt8188_etdm_id of the master port, or
 *                    MT8188_COWORK_SOURCE_NONE to disable cowork.
 *
 * Linux equivalent: mt8188_dai_etdm_parse_of() stores a raw DAI ID in
 * cowork_source_id. Match that here — pass enum mt8188_etdm_id directly.
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_set_cowork_source(struct mt8188_afe *afe, enum mt8188_etdm_id id,
				  int cowork_source_id)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	afe->etdm[id].cowork_source_id = cowork_source_id;
	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_set_in_disable_ch — mark channels as disabled (IN paths only)
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_set_in_disable_ch(struct mt8188_afe *afe, enum mt8188_etdm_id id,
				  const uint8_t *disabled_chs, uint32_t count)
{
	uint32_t i;

	if (id != MT8188_ETDM_IN1 && id != MT8188_ETDM_IN2) {
		return -EINVAL;
	}
	if (count > 0U && disabled_chs == NULL) {
		return -EINVAL;
	}

	/*
	 * The set replaces what was there rather than adding to it, so a port
	 * reused by a later stream does not inherit channels the previous one
	 * masked off.
	 */
	memset(afe->etdm[id].in_disable_ch, 0, sizeof(afe->etdm[id].in_disable_ch));

	for (i = 0; i < count; i++) {
		if (disabled_chs[i] >= MT8188_ETDM_MAX_CHANNELS) {
			return -EINVAL;
		}
		afe->etdm[id].in_disable_ch[disabled_chs[i]] = true;
	}
	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_update_sync_info — build slave→master cowork linkage
 * Linux: mt8188_etdm_update_sync_info()
 * Call once after all set_cowork_source() calls, before configure().
 * -------------------------------------------------------------------------
 */
void mt8188_etdm_update_sync_info(struct mt8188_afe *afe)
{
	int i;

	/* Reset all slave counts */
	for (i = 0; i < MT8188_ETDM_NR; i++) {
		afe->etdm[i].cowork_slv_count = 0;
	}

	/* For each port that has a cowork source, register it as slave in master.
	 * cowork_source_id is an enum mt8188_etdm_id — used directly as the
	 * master array index.
	 */
	for (i = 0; i < MT8188_ETDM_NR; i++) {
		int mst_id = afe->etdm[i].cowork_source_id;

		if (mst_id == MT8188_COWORK_SOURCE_NONE) {
			continue;
		}
		if (mst_id < 0 || mst_id >= MT8188_ETDM_NR) {
			continue;
		}
		struct mt8188_etdm_config *mst = &afe->etdm[mst_id];

		if (mst->cowork_slv_count < (uint32_t)(MT8188_ETDM_NR - 1)) {
			mst->cowork_slv_id[mst->cowork_slv_count] = i;
			mst->cowork_slv_count++;
		}
	}
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_configure — write all CON registers for a port
 * Linux: mtk_dai_etdm_configure() + hw_params() cowork slave loop
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_configure(struct mt8188_afe *afe, enum mt8188_etdm_id id,
			  const struct mt8188_afe_cfg *cfg)
{
	struct mt8188_etdm_config *ecfg;
	const struct etdm_regs *r;
	uint32_t channels, wlen, etdm_channels, bck;
	uint32_t mask, val;
	int ret;
	uint32_t i;

	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	ecfg = &afe->etdm[id];
	r = &etdm_reg_table[id];

	/* Apply slot override if set */
	channels = (ecfg->slots > 0) ? ecfg->slots : cfg->channels;
	wlen = get_etdm_wlen(cfg->word_size);

	/* Store fmt from cfg if not already set via set_fmt() */
	if (!ecfg->configured) {
		ecfg->fmt = cfg->fmt;
	}
	ecfg->rate = cfg->rate;
	ecfg->configured = true;

	/* ONE_PIN mode: round up to power-of-2 channel count */
	etdm_channels =
		(ecfg->data_mode == MT8188_ETDM_DATA_ONE_PIN) ? get_etdm_ch_fixup(channels) : 2;

	/* BCK rate validation */
	bck = cfg->rate * etdm_channels * wlen;
	if (bck > MT8188_ETDM_NORMAL_MAX_BCK_RATE) {
		return -EINVAL;
	}

	/* CON0: format, channel count, bit/word length, slave mode */
	mask = ETDM_CON0_BIT_LEN_MASK | ETDM_CON0_WORD_LEN_MASK | ETDM_CON0_FORMAT_MASK |
	       ETDM_CON0_CH_NUM_MASK | ETDM_CON0_SLAVE_MODE;
	val = FIELD_PREP(ETDM_CON0_BIT_LEN_MASK, cfg->word_size - 1) |
	      FIELD_PREP(ETDM_CON0_WORD_LEN_MASK, wlen - 1) |
	      FIELD_PREP(ETDM_CON0_FORMAT_MASK, (uint32_t)ecfg->fmt) |
	      FIELD_PREP(ETDM_CON0_CH_NUM_MASK, etdm_channels - 1);
	if (ecfg->slave_mode) {
		val |= ETDM_CON0_SLAVE_MODE;
	}
	reg_update_bits(afe->base, r->con0, mask, val);

	/* IN or OUT specific CON1–CON5 */
	if (id == MT8188_ETDM_IN1 || id == MT8188_ETDM_IN2) {
		ret = etdm_in_configure(afe->base, id, cfg->rate, channels, ecfg);
	} else {
		ret = etdm_out_configure(afe->base, id, cfg->rate, channels, ecfg);
	}
	if (ret) {
		return ret;
	}

	/* MCLK source: a1sys/a2sys (1 = driver default for all eTDM ports) */
	etdm_set_clock_source(afe->base, id, 1);

	/* Cowork sync for this port */
	ret = etdm_sync_mode_configure(afe, id, ecfg);
	if (ret) {
		return ret;
	}

	/* Propagate configuration to all registered cowork slaves */
	for (i = 0; i < ecfg->cowork_slv_count; i++) {
		int slv_id = ecfg->cowork_slv_id[i];

		if (slv_id < 0 || slv_id >= MT8188_ETDM_NR) {
			continue;
		}
		struct mt8188_etdm_config *slv_ecfg = &afe->etdm[slv_id];
		const struct etdm_regs *sr = &etdm_reg_table[slv_id];

		/* CON0 for slave — use slave's own fmt, same ch/len as master */
		mask = ETDM_CON0_BIT_LEN_MASK | ETDM_CON0_WORD_LEN_MASK | ETDM_CON0_FORMAT_MASK |
		       ETDM_CON0_CH_NUM_MASK | ETDM_CON0_SLAVE_MODE;
		val = FIELD_PREP(ETDM_CON0_BIT_LEN_MASK, cfg->word_size - 1) |
		      FIELD_PREP(ETDM_CON0_WORD_LEN_MASK, wlen - 1) |
		      FIELD_PREP(ETDM_CON0_FORMAT_MASK, (uint32_t)slv_ecfg->fmt) |
		      FIELD_PREP(ETDM_CON0_CH_NUM_MASK, etdm_channels - 1) | ETDM_CON0_SLAVE_MODE;
		reg_update_bits(afe->base, sr->con0, mask, val);

		/* IN/OUT sub-configuration for slave */
		if (slv_id == MT8188_ETDM_IN1 || slv_id == MT8188_ETDM_IN2) {
			etdm_in_configure(afe->base, (enum mt8188_etdm_id)slv_id, cfg->rate,
					  channels, slv_ecfg);
		} else {
			etdm_out_configure(afe->base, (enum mt8188_etdm_id)slv_id, cfg->rate,
					   channels, slv_ecfg);
		}

		etdm_set_clock_source(afe->base, (enum mt8188_etdm_id)slv_id, 1);
		etdm_sync_mode_configure(afe, (enum mt8188_etdm_id)slv_id, slv_ecfg);
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_etdm_start / mt8188_etdm_stop
 * -------------------------------------------------------------------------
 */
int mt8188_etdm_start(struct mt8188_afe *afe, enum mt8188_etdm_id id)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	/* Enable global AFE */
	reg_set_bits(afe->base, AFE_DAC_CON0, BIT(0));
	/* Enable eTDM port */
	reg_set_bits(afe->base, etdm_reg_table[id].con0, ETDM_CON0_EN);
	return 0;
}

int mt8188_etdm_stop(struct mt8188_afe *afe, enum mt8188_etdm_id id)
{
	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	reg_update_bits(afe->base, etdm_reg_table[id].con0, ETDM_CON0_EN, 0);
	return 0;
}

/* =========================================================================
 * AFE_CONN routing table
 * =========================================================================
 */

struct conn_entry {
	uint32_t reg;
	uint8_t bit;
};

/*
 * UL9 32-channel route via AFE_CONN matrix (CM0 in bypass mode).
 *
 * Per the MT8188 SoC datasheet, UL9 captures 32 channels through:
 *   UL9 ← CM0 (bypass) ← AFE_CONN ← eTDM_IN1 + eTDM_IN2
 *
 * The AFE_CONN matrix routes 32 input channels to output pins O002..O033,
 * which CM0 reads; CM0 (in bypass) forwards them unchanged to UL9. This
 * config is NOT present in the Linux mainline driver — it is MT8188-specific.
 *
 * Channel layout (O-pin → eTDM input channel):
 *   O002..O017 ← eTDM_IN1  I072..I087  (16 channels)
 *   O018..O025 ← eTDM_IN2  I012..I019  (8 channels)
 *   O026..O033 ← eTDM_IN2  I188..I195  (8 channels)
 *
 * AFE_CONN word layout: AFE_CONN<O> covers input bits 0..31 (word 0),
 * AFE_CONN<O>_5 covers inputs 160..191, AFE_CONN<O>_6 covers inputs 192+.
 * For O-pin O and input I: set bit (I mod 32) in word (I / 32).
 */
static const struct conn_entry conn_etdm_in1_cm0[] = {
	/* O002..O017 ← eTDM_IN1 I072..I087 (word 2 = inputs 64..95) */
	{AFE_CONN2_2, 8},   /* O002 ← I072 */
	{AFE_CONN3_2, 9},   /* O003 ← I073 */
	{AFE_CONN4_2, 10},  /* O004 ← I074 */
	{AFE_CONN5_2, 11},  /* O005 ← I075 */
	{AFE_CONN6_2, 12},  /* O006 ← I076 */
	{AFE_CONN7_2, 13},  /* O007 ← I077 */
	{AFE_CONN8_2, 14},  /* O008 ← I078 */
	{AFE_CONN9_2, 15},  /* O009 ← I079 */
	{AFE_CONN10_2, 16}, /* O010 ← I080 */
	{AFE_CONN11_2, 17}, /* O011 ← I081 */
	{AFE_CONN12_2, 18}, /* O012 ← I082 */
	{AFE_CONN13_2, 19}, /* O013 ← I083 */
	{AFE_CONN14_2, 20}, /* O014 ← I084 */
	{AFE_CONN15_2, 21}, /* O015 ← I085 */
	{AFE_CONN16_2, 22}, /* O016 ← I086 */
	{AFE_CONN17_2, 23}, /* O017 ← I087 */
};

/*
 * UL9 upper 16 channels: eTDM_IN2 → O018..O033 (CM0 in bypass).
 *
 * Only used for 32-channel UL9. At ≤16 channels UL9 takes eTDM_IN1 alone
 * (conn_etdm_in1_cm0 above) and eTDM_IN2 stays free for UL3.
 *
 * eTDM_IN2's 16 channels are split across two non-contiguous input ranges:
 * I012..I019 (word 0) and I188..I195 (words 5/6).
 */
static const struct conn_entry conn_etdm_in2_cm0[] = {
	/* O018..O025 ← eTDM_IN2 I012..I019 (word 0 = inputs 0..31) */
	{AFE_CONN18, 12}, /* O018 ← I012 */
	{AFE_CONN19, 13}, /* O019 ← I013 */
	{AFE_CONN20, 14}, /* O020 ← I014 */
	{AFE_CONN21, 15}, /* O021 ← I015 */
	{AFE_CONN22, 16}, /* O022 ← I016 */
	{AFE_CONN23, 17}, /* O023 ← I017 */
	{AFE_CONN24, 18}, /* O024 ← I018 */
	{AFE_CONN25, 19}, /* O025 ← I019 */
	/* O026..O029 ← eTDM_IN2 I188..I191 (word 5 = inputs 160..191) */
	{AFE_CONN26_5, 28}, /* O026 ← I188 */
	{AFE_CONN27_5, 29}, /* O027 ← I189 */
	{AFE_CONN28_5, 30}, /* O028 ← I190 */
	{AFE_CONN29_5, 31}, /* O029 ← I191 */
	/* O030..O033 ← eTDM_IN2 I192..I195 (word 6 = inputs 192..223) */
	{AFE_CONN30_6, 0}, /* O030 ← I192 */
	{AFE_CONN31_6, 1}, /* O031 ← I193 */
	{AFE_CONN32_6, 2}, /* O032 ← I194 */
	{AFE_CONN33_6, 3}, /* O033 ← I195 */
};

/*
 * Playback: DL11 direct range → eTDM_OUT1.
 *
 * DL11 drives I022..I037 directly; those map to eTDM_OUT1's 16 output pins
 * O072..O087. Bit positions follow the universal AFE_CONN rule (bit = I % 32,
 * word = I / 32), so I022..I031 land in word 0 (bits 22..31) and I032..I037
 * in word 1 (bits 0..5). Matches Linux mtk_dai_etdm_o072_mix..o087_mix.
 */
static const struct conn_entry conn_dl11_etdm_out1[] = {
	/* O072..O081 ← I022..I031 (word 0) */
	{AFE_CONN72, 22}, /* O072 ← I022 */
	{AFE_CONN73, 23}, /* O073 ← I023 */
	{AFE_CONN74, 24}, /* O074 ← I024 */
	{AFE_CONN75, 25}, /* O075 ← I025 */
	{AFE_CONN76, 26}, /* O076 ← I026 */
	{AFE_CONN77, 27}, /* O077 ← I027 */
	{AFE_CONN78, 28}, /* O078 ← I028 */
	{AFE_CONN79, 29}, /* O079 ← I029 */
	{AFE_CONN80, 30}, /* O080 ← I030 */
	{AFE_CONN81, 31}, /* O081 ← I031 */
	/* O082..O087 ← I032..I037 (word 1) */
	{AFE_CONN82_1, 0}, /* O082 ← I032 */
	{AFE_CONN83_1, 1}, /* O083 ← I033 */
	{AFE_CONN84_1, 2}, /* O084 ← I034 */
	{AFE_CONN85_1, 3}, /* O085 ← I035 */
	{AFE_CONN86_1, 4}, /* O086 ← I036 */
	{AFE_CONN87_1, 5}, /* O087 ← I037 */
};

/*
 * Playback: DL8_DL11 mux range → eTDM_OUT2.
 *
 * I046..I061 is the 16-channel range shared by DL8 and DL11 through the
 * DL8_DL11 mux (AFE_DAC_CON2 bit 0); it maps to eTDM_OUT2's pins O048..O063.
 * I046..I061 are all in word 1, so every entry uses AFE_CONN<N>_1 with
 * bit = 14 + n. Matches Linux mtk_dai_etdm_o048_mix..o063_mix.
 *
 * Used by DL8 16ch playback (mux=dl8) and by the upper 16 channels of DL11
 * 32ch playback (mux=dl11).
 */
static const struct conn_entry conn_dl8_dl11_etdm_out2[] = {
	{AFE_CONN48_1, 14}, /* O048 ← I046 */
	{AFE_CONN49_1, 15}, /* O049 ← I047 */
	{AFE_CONN50_1, 16}, /* O050 ← I048 */
	{AFE_CONN51_1, 17}, /* O051 ← I049 */
	{AFE_CONN52_1, 18}, /* O052 ← I050 */
	{AFE_CONN53_1, 19}, /* O053 ← I051 */
	{AFE_CONN54_1, 20}, /* O054 ← I052 */
	{AFE_CONN55_1, 21}, /* O055 ← I053 */
	{AFE_CONN56_1, 22}, /* O056 ← I054 */
	{AFE_CONN57_1, 23}, /* O057 ← I055 */
	{AFE_CONN58_1, 24}, /* O058 ← I056 */
	{AFE_CONN59_1, 25}, /* O059 ← I057 */
	{AFE_CONN60_1, 26}, /* O060 ← I058 */
	{AFE_CONN61_1, 27}, /* O061 ← I059 */
	{AFE_CONN62_1, 28}, /* O062 ← I060 */
	{AFE_CONN63_1, 29}, /* O063 ← I061 */
};

int mt8188_afe_set_route(struct mt8188_afe *afe, enum mt8188_route_src src,
			 enum mt8188_route_dst dst)
{
	/* Up to two tables: DL11 32ch drives eTDM_OUT1 and eTDM_OUT2 together. */
	const struct conn_entry *tbl[2] = {NULL, NULL};
	size_t tbl_n[2] = {0, 0};
	size_t t, i;

	if ((unsigned int)src >= MT8188_ROUTE_SRC_NR || (unsigned int)dst >= MT8188_ROUTE_DST_NR) {
		return -EINVAL;
	}

#define ROUTE_TBL(slot, arr)                                                                       \
	do {                                                                                       \
		tbl[slot] = (arr);                                                                 \
		tbl_n[slot] = ARRAY_SIZE(arr);                                                     \
	} while (0)

#define CLEAR_TBL(arr)                                                                             \
	do {                                                                                       \
		size_t n;                                                                          \
                                                                                                   \
		for (n = 0; n < ARRAY_SIZE(arr); n++) {                                            \
			sys_write32(sys_read32(afe->base + (arr)[n].reg) & ~BIT((arr)[n].bit),     \
				    afe->base + (arr)[n].reg);                                     \
		}                                                                                  \
	} while (0)

	switch (src) {
	case MT8188_ROUTE_SRC_ETDM_IN1:
		/* UL8 ← ETDM_IN1: hardware-wired, no CONN bits needed */
		if (dst == MT8188_ROUTE_DST_UL8) {
			return 0;
		}
		/* UL9 ≤16ch ← CM0 (bypass) ← AFE_CONN ← eTDM_IN1 only.
		 * eTDM_IN2 is left free for a concurrent UL3 stream.
		 */
		if (dst == MT8188_ROUTE_DST_UL9) {
			ROUTE_TBL(0, conn_etdm_in1_cm0);
		}
		break;
	case MT8188_ROUTE_SRC_ETDM_IN2:
		/* UL3 ← ETDM_IN2: hardware-wired */
		if (dst == MT8188_ROUTE_DST_UL3) {
			return 0;
		}
		break;
	case MT8188_ROUTE_SRC_ETDM_IN1_IN2:
		/* UL9 ← CM0 (bypass) ← AFE_CONN ← eTDM_IN1 + eTDM_IN2.
		 * Per the MT8188 datasheet, the 32-channel route is built in the
		 * AFE_CONN matrix (O002..O033 ← IN1 I072..I087 + IN2 I012..I019,
		 * I188..I195). CM0 runs in bypass; its cowork clock sync is set
		 * up at configure() time via mt8188_etdm_set_cowork_source().
		 */
		if (dst == MT8188_ROUTE_DST_UL9) {
			ROUTE_TBL(0, conn_etdm_in1_cm0);
			ROUTE_TBL(1, conn_etdm_in2_cm0);
		}
		break;
	case MT8188_ROUTE_SRC_DL8:
		/* DL8 → eTDM_OUT2 via the DL8_DL11 mux range (I046..I061).
		 * Caller must select mux=dl8 (AFE_DAC_CON2 b0 = 0).
		 */
		if (dst == MT8188_ROUTE_DST_ETDM_OUT2) {
			ROUTE_TBL(0, conn_dl8_dl11_etdm_out2);
		}
		break;
	case MT8188_ROUTE_SRC_DL11:
		if (dst == MT8188_ROUTE_DST_ETDM_OUT1) {
			/* 16ch: DL11 direct range only */
			ROUTE_TBL(0, conn_dl11_etdm_out1);
		} else if (dst == MT8188_ROUTE_DST_ETDM_OUT1_OUT2) {
			/* 32ch: ch0-15 direct → OUT1, ch16-31 via mux → OUT2.
			 * Caller must select mux=dl11 (AFE_DAC_CON2 b0 = 1).
			 */
			ROUTE_TBL(0, conn_dl11_etdm_out1);
			ROUTE_TBL(1, conn_dl8_dl11_etdm_out2);
		}
		break;
	default:
		break;
	}

#undef ROUTE_TBL

	if (tbl[0] == NULL) {
		return -ENOTSUP;
	}

	/*
	 * Clear the destination's rows before programming them.
	 *
	 * A matrix row sums every input selected in it, so setting bits without
	 * clearing accumulates: a 32-channel capture followed by a 16-channel
	 * one on the same interface would leave the second port still feeding
	 * the merge unit, contradicting the port state configure() has by then
	 * released.  Only rows belonging to this destination are cleared, so a
	 * stream running to the other port keeps its route.
	 */
	switch (dst) {
	case MT8188_ROUTE_DST_UL9:
		CLEAR_TBL(conn_etdm_in1_cm0);
		CLEAR_TBL(conn_etdm_in2_cm0);
		break;

	case MT8188_ROUTE_DST_ETDM_OUT1:
		CLEAR_TBL(conn_dl11_etdm_out1);
		break;

	case MT8188_ROUTE_DST_ETDM_OUT2:
		CLEAR_TBL(conn_dl8_dl11_etdm_out2);
		break;

	case MT8188_ROUTE_DST_ETDM_OUT1_OUT2:
		CLEAR_TBL(conn_dl11_etdm_out1);
		CLEAR_TBL(conn_dl8_dl11_etdm_out2);
		break;

	default:
		/* UL3 and UL8 are wired straight through and have no rows. */
		break;
	}

	for (t = 0; t < ARRAY_SIZE(tbl); t++) {
		for (i = 0; i < tbl_n[t]; i++) {
			sys_write32(sys_read32(afe->base + tbl[t][i].reg) | BIT(tbl[t][i].bit),
				    afe->base + tbl[t][i].reg);
		}
	}

	return 0;
}

#undef CLEAR_TBL
