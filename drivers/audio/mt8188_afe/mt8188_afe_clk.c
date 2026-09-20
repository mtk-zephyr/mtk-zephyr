/*
 * Copyright (c) 2026 MediaTek Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MT8188 AFE clock enable/disable — 4-tier sequence.
 * Ported from Linux mt8188-afe-clk.c mt8188_afe_enable_clocks().
 *
 * Tier 1: APMIXEDSYS  — APLL1 (48kHz) or APLL2 (44.1kHz) PLL
 * Tier 2: TOPCKGEN    — audio mux+gate clocks
 * Tier 3: INFRACFG_AO — infra audio gate clocks
 * Tier 4: AUDSYS      — direct MMIO on AFE base
 *
 * Note: CLK_TOP_APLL1_D4 / CLK_TOP_APLL2_D4 are fixed dividers derived
 * from APLL1/APLL2 — always available when the parent PLL is on. Their
 * clock_control_on() may return -ENOTSUP from the TOPCKGEN driver stub;
 * this is expected and ignored here.
 */

#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/mtk_mt8188_clock.h>

#include "mt8188_afe_priv.h"
#include "mt8188_afe_audsys.h"
#include "mt8188_afe_reg.h"

/*
 * ADSP audio 26M clock gate.
 * Linux: "adsp_audio_26m" obtained via CCF from &adsp_audio26m DT node.
 * Physical base: 0x10b91100, register offset 0x80, bit 3.
 * CLK_GATE_SET_TO_DISABLE convention: set bit = disabled.
 *
 * The mapped virtual address is stored in afe->adsp_audio26m, set up by
 * device_map() in mt8188_afe_init().
 */
#define ADSP_AUDIO26M_CON0 0x80U
#define ADSP_AUDIO26M_BIT  BIT(3)

static void adsp_audio26m_enable(struct mt8188_afe *afe)
{
	uintptr_t reg = afe->adsp_audio26m + ADSP_AUDIO26M_CON0;

	sys_write32(sys_read32(reg) & ~ADSP_AUDIO26M_BIT, reg);
}

static void adsp_audio26m_disable(struct mt8188_afe *afe)
{
	uintptr_t reg = afe->adsp_audio26m + ADSP_AUDIO26M_CON0;

	sys_write32(sys_read32(reg) | ADSP_AUDIO26M_BIT, reg);
}

/* Ignore -ENOTSUP for fixed-divider stubs (e.g. APLL1_D4, APLL2_D4)
 *
 * clock_control_subsys_t is a void *, so the clock ID goes through uintptr_t
 * first: on LP64 a direct uint32_t → void * cast trips
 * -Wint-to-pointer-cast ("cast to pointer from integer of different size").
 * Integer literals are special-cased by the compiler, but variables — e.g.
 * the uint32_t fields of etdm_port_clk_table — are not.
 */
static inline int clk_on_tolerant(const struct device *dev, uint32_t id)
{
	int ret = clock_control_on(dev, (clock_control_subsys_t)(uintptr_t)id);

	return (ret == -ENOTSUP) ? 0 : ret;
}

static inline int clk_off_tolerant(const struct device *dev, uint32_t id)
{
	int ret = clock_control_off(dev, (clock_control_subsys_t)(uintptr_t)id);

	return (ret == -ENOTSUP) ? 0 : ret;
}

/*
 * Turning a clock off is best effort.  A teardown that stopped at the first
 * refusal would leave the rest of the path running, which is worse than
 * carrying on, so the disable paths below discard the result deliberately
 * while the enable paths propagate it.
 */

/* -------------------------------------------------------------------------
 * mt8188_afe_enable_main_clock / disable_main_clock
 * Linux: mt8188_afe_enable_main_clock() / mt8188_afe_disable_main_clock()
 *
 * Operations:
 *   enable:  set ASYS_TOP_CON_26M_TIMING_ON, then set AFE_DAC_CON0 bit 0
 *   disable: clear AFE_DAC_CON0 bit 0, then clear ASYS_TOP_CON_26M_TIMING_ON
 * -------------------------------------------------------------------------
 */
void mt8188_afe_enable_main_clock(struct mt8188_afe *afe)
{
	/* 26M timing — ASYS_TOP_CON bit 2 (set = on, non-inverted) */
	sys_write32(sys_read32(afe->base + ASYS_TOP_CON) | ASYS_TOP_CON_26M_TIMING_ON,
		    afe->base + ASYS_TOP_CON);

	/* AFE global enable — AFE_DAC_CON0 bit 0 */
	sys_write32(sys_read32(afe->base + AFE_DAC_CON0) | BIT(0), afe->base + AFE_DAC_CON0);
}

void mt8188_afe_disable_main_clock(struct mt8188_afe *afe)
{
	/* AFE global disable */
	sys_write32(sys_read32(afe->base + AFE_DAC_CON0) & ~BIT(0), afe->base + AFE_DAC_CON0);

	/* 26M timing off */
	sys_write32(sys_read32(afe->base + ASYS_TOP_CON) & ~ASYS_TOP_CON_26M_TIMING_ON,
		    afe->base + ASYS_TOP_CON);
}

/* -------------------------------------------------------------------------
 * APLL tuner configuration — ported from Linux struct mt8188_afe_tuner_cfg
 * and mt8188_afe_tuner_cfgs[] in mt8188-afe-clk.c.
 *
 * Each entry describes the register layout of one PLL tuner instance.
 * mt8188_afe_tuner_enable/disable operate generically on any entry.
 * -------------------------------------------------------------------------
 */
struct mt8188_afe_tuner_cfg {
	uint32_t apll_div_reg;
	uint8_t apll_div_shift;
	uint32_t apll_div_mask;
	uint8_t apll_div_default;
	uint32_t ref_ck_sel_reg;
	uint8_t ref_ck_sel_shift;
	uint32_t ref_ck_sel_mask;
	uint8_t ref_ck_sel_default;
	uint32_t tuner_en_reg;
	uint8_t tuner_en_shift;
	uint32_t upper_bound_reg;
	uint8_t upper_bound_shift;
	uint32_t upper_bound_mask;
	uint8_t upper_bound_default;
	/* AUDIO_TOP_CON0 PDN gates feeding this tuner (Linux
	 * mt8188_afe_enable_tuner_clk). Only PLL1/PLL2 have them; for the
	 * other PLLs set has_audsys_gates = false.
	 */
	bool has_audsys_gates;
	enum mt8188_audsys_clk_id feeder_gate; /* CLK_AUD_APLL / _APLL2 */
	enum mt8188_audsys_clk_id tuner_gate;  /* CLK_AUD_APLL{1,2}_TUNER */
};

enum mt8188_aud_pll {
	MT8188_AUD_PLL1 = 0,
	MT8188_AUD_PLL2,
	MT8188_AUD_PLL3,
	MT8188_AUD_PLL4,
	MT8188_AUD_PLL5,
	MT8188_AUD_PLL_NUM,
};

/*
 * The descriptor tables below are laid out by hand to mirror the datasheet.
 * They are excluded from clang-format because it moves the brace of a
 * designated initializer onto its own line, which checkpatch rejects.
 */
/* clang-format off */
static const struct mt8188_afe_tuner_cfg
	mt8188_afe_tuner_cfgs[MT8188_AUD_PLL_NUM] = {
	[MT8188_AUD_PLL1] = {
		.apll_div_reg       = AFE_APLL_TUNER_CFG,
		.apll_div_shift     = 4,
		.apll_div_mask      = 0xf,
		.apll_div_default   = 0x7,
		.ref_ck_sel_reg     = AFE_APLL_TUNER_CFG,
		.ref_ck_sel_shift   = 1,
		.ref_ck_sel_mask    = 0x3,
		.ref_ck_sel_default = 0x2,
		.tuner_en_reg       = AFE_APLL_TUNER_CFG,
		.tuner_en_shift     = 0,
		.upper_bound_reg    = AFE_APLL_TUNER_CFG,
		.upper_bound_shift  = 8,
		.upper_bound_mask   = 0xff,
		.upper_bound_default = 0x3,
		.has_audsys_gates   = true,
		.feeder_gate        = MT8188_CLK_AUD_APLL,
		.tuner_gate         = MT8188_CLK_AUD_APLL1_TUNER,
	},
	[MT8188_AUD_PLL2] = {
		.apll_div_reg       = AFE_APLL_TUNER_CFG1,
		.apll_div_shift     = 4,
		.apll_div_mask      = 0xf,
		.apll_div_default   = 0x7,
		.ref_ck_sel_reg     = AFE_APLL_TUNER_CFG1,
		.ref_ck_sel_shift   = 1,
		.ref_ck_sel_mask    = 0x3,
		.ref_ck_sel_default = 0x1,
		.tuner_en_reg       = AFE_APLL_TUNER_CFG1,
		.tuner_en_shift     = 0,
		.upper_bound_reg    = AFE_APLL_TUNER_CFG1,
		.upper_bound_shift  = 8,
		.upper_bound_mask   = 0xff,
		.upper_bound_default = 0x3,
		.has_audsys_gates   = true,
		.feeder_gate        = MT8188_CLK_AUD_APLL2,
		.tuner_gate         = MT8188_CLK_AUD_APLL2_TUNER,
	},
	[MT8188_AUD_PLL3] = {
		.apll_div_reg       = AFE_EARC_APLL_TUNER_CFG,
		.apll_div_shift     = 4,
		.apll_div_mask      = 0x3f,
		.apll_div_default   = 0x3,
		.ref_ck_sel_reg     = AFE_EARC_APLL_TUNER_CFG,
		.ref_ck_sel_shift   = 24,
		.ref_ck_sel_mask    = 0x3,
		.ref_ck_sel_default = 0x0,
		.tuner_en_reg       = AFE_EARC_APLL_TUNER_CFG,
		.tuner_en_shift     = 0,
		.upper_bound_reg    = AFE_EARC_APLL_TUNER_CFG,
		.upper_bound_shift  = 12,
		.upper_bound_mask   = 0xff,
		.upper_bound_default = 0x4,
	},
	[MT8188_AUD_PLL4] = {
		.apll_div_reg       = AFE_SPDIFIN_APLL_TUNER_CFG,
		.apll_div_shift     = 4,
		.apll_div_mask      = 0x3f,
		.apll_div_default   = 0x7,
		.ref_ck_sel_reg     = AFE_SPDIFIN_APLL_TUNER_CFG1,
		.ref_ck_sel_shift   = 8,
		.ref_ck_sel_mask    = 0x1,
		.ref_ck_sel_default = 0,
		.tuner_en_reg       = AFE_SPDIFIN_APLL_TUNER_CFG,
		.tuner_en_shift     = 0,
		.upper_bound_reg    = AFE_SPDIFIN_APLL_TUNER_CFG,
		.upper_bound_shift  = 12,
		.upper_bound_mask   = 0xff,
		.upper_bound_default = 0x4,
	},
	[MT8188_AUD_PLL5] = {
		.apll_div_reg       = AFE_LINEIN_APLL_TUNER_CFG,
		.apll_div_shift     = 4,
		.apll_div_mask      = 0x3f,
		.apll_div_default   = 0x3,
		.ref_ck_sel_reg     = AFE_LINEIN_APLL_TUNER_CFG,
		.ref_ck_sel_shift   = 24,
		.ref_ck_sel_mask    = 0x1,
		.ref_ck_sel_default = 0,
		.tuner_en_reg       = AFE_LINEIN_APLL_TUNER_CFG,
		.tuner_en_shift     = 0,
		.upper_bound_reg    = AFE_LINEIN_APLL_TUNER_CFG,
		.upper_bound_shift  = 12,
		.upper_bound_mask   = 0xff,
		.upper_bound_default = 0x4,
	},
};
/* clang-format on */

/*
 * mt8188_afe_tuner_enable / mt8188_afe_tuner_disable
 * Linux: mt8188_afe_setup_apll_tuner() + mt8188_afe_enable_tuner_clk()
 *        + enable/disable tuner_en bit.
 */
static void mt8188_afe_tuner_enable(struct mt8188_afe *afe, const struct mt8188_afe_tuner_cfg *cfg)
{
	uint32_t val;

	/* Setup apll_div */
	val = sys_read32(afe->base + cfg->apll_div_reg);
	val &= ~(cfg->apll_div_mask << cfg->apll_div_shift);
	val |= ((uint32_t)cfg->apll_div_default << cfg->apll_div_shift);
	sys_write32(val, afe->base + cfg->apll_div_reg);

	/* Setup ref_ck_sel */
	val = sys_read32(afe->base + cfg->ref_ck_sel_reg);
	val &= ~(cfg->ref_ck_sel_mask << cfg->ref_ck_sel_shift);
	val |= ((uint32_t)cfg->ref_ck_sel_default << cfg->ref_ck_sel_shift);
	sys_write32(val, afe->base + cfg->ref_ck_sel_reg);

	/* Setup upper_bound */
	val = sys_read32(afe->base + cfg->upper_bound_reg);
	val &= ~(cfg->upper_bound_mask << cfg->upper_bound_shift);
	val |= ((uint32_t)cfg->upper_bound_default << cfg->upper_bound_shift);
	sys_write32(val, afe->base + cfg->upper_bound_reg);

	/* Ungate the APLL feeder + tuner clocks (AUDIO_TOP_CON0 PDN bits) —
	 * Linux mt8188_afe_enable_tuner_clk(): feeder first, then tuner.
	 */
	if (cfg->has_audsys_gates) {
		mt8188_audsys_clk_on(afe->base, cfg->feeder_gate);
		mt8188_audsys_clk_on(afe->base, cfg->tuner_gate);
	}

	/* Enable tuner */
	sys_write32(sys_read32(afe->base + cfg->tuner_en_reg) | BIT(cfg->tuner_en_shift),
		    afe->base + cfg->tuner_en_reg);
}

static void mt8188_afe_tuner_disable(struct mt8188_afe *afe, const struct mt8188_afe_tuner_cfg *cfg)
{
	sys_write32(sys_read32(afe->base + cfg->tuner_en_reg) & ~BIT(cfg->tuner_en_shift),
		    afe->base + cfg->tuner_en_reg);

	/* Gate the tuner + feeder clocks — Linux mt8188_afe_disable_tuner_clk():
	 * tuner first, then feeder (reverse of enable).
	 */
	if (cfg->has_audsys_gates) {
		mt8188_audsys_clk_off(afe->base, cfg->tuner_gate);
		mt8188_audsys_clk_off(afe->base, cfg->feeder_gate);
	}
}

/* -------------------------------------------------------------------------
 * mt8188_afe_enable_a1sys / disable_a1sys
 * Linux: mt8188_afe_enable_a1sys() / mt8188_afe_disable_a1sys()
 *
 * Operations:
 *   enable:  CLK_AUD_A1SYS gate + ASYS_TOP_CON_A1SYS_TIMING_ON
 *   disable: clear timing bit, then CLK_AUD_A1SYS gate off
 * -------------------------------------------------------------------------
 */
static void mt8188_afe_enable_a1sys(struct mt8188_afe *afe)
{
	mt8188_audsys_clk_on(afe->base, MT8188_CLK_AUD_A1SYS);
	sys_write32(sys_read32(afe->base + ASYS_TOP_CON) | ASYS_TOP_CON_A1SYS_TIMING_ON,
		    afe->base + ASYS_TOP_CON);
}

static void mt8188_afe_disable_a1sys(struct mt8188_afe *afe)
{
	sys_write32(sys_read32(afe->base + ASYS_TOP_CON) & ~ASYS_TOP_CON_A1SYS_TIMING_ON,
		    afe->base + ASYS_TOP_CON);
	mt8188_audsys_clk_off(afe->base, MT8188_CLK_AUD_A1SYS);
}

/* -------------------------------------------------------------------------
 * mt8188_afe_enable_a2sys / disable_a2sys
 * Linux: mt8188_afe_enable_a2sys() / mt8188_afe_disable_a2sys()
 *
 * Operations:
 *   enable:  CLK_AUD_A2SYS gate + ASYS_TOP_CON_A2SYS_TIMING_ON
 *   disable: clear timing bit, then CLK_AUD_A2SYS gate off
 * -------------------------------------------------------------------------
 */
static void mt8188_afe_enable_a2sys(struct mt8188_afe *afe)
{
	mt8188_audsys_clk_on(afe->base, MT8188_CLK_AUD_A2SYS);
	sys_write32(sys_read32(afe->base + ASYS_TOP_CON) | ASYS_TOP_CON_A2SYS_TIMING_ON,
		    afe->base + ASYS_TOP_CON);
}

static void mt8188_afe_disable_a2sys(struct mt8188_afe *afe)
{
	sys_write32(sys_read32(afe->base + ASYS_TOP_CON) & ~ASYS_TOP_CON_A2SYS_TIMING_ON,
		    afe->base + ASYS_TOP_CON);
	mt8188_audsys_clk_off(afe->base, MT8188_CLK_AUD_A2SYS);
}

/* -------------------------------------------------------------------------
 * A1SYS_HP mux parent select helper.
 * Linux: clk_set_parent(A1SYS_HP_SEL, APLL1_D4) / clk_set_parent(..., clk26m)
 *   parent[0]=clk26m, parent[1]=apll1_d4
 *
 * Parent select and gate enable are independent: A1SYS_HP is a dynamic-parent
 * clock in the topckgen table, so clock_control_on/off() toggle only the gate
 * (they do not rewrite the mux), and .configure() owns the parent. This lets
 * the parent set here survive the gate enable.
 * -------------------------------------------------------------------------
 */
static void a1sys_hp_set_parent(struct mt8188_afe *afe, uint8_t parent_idx)
{
	clock_control_configure(afe->clk_topckgen,
				(clock_control_subsys_t)(uintptr_t)CLK_TOP_A1SYS_HP, &parent_idx);
}

/* -------------------------------------------------------------------------
 * mt8188_apll1_enable / mt8188_apll1_disable
 * Linux: mt8188_apll1_enable() / mt8188_apll1_disable()
 *
 * mt8188_apll1_enable sequence:
 *   1. enable CLK_TOP_APLL1_D4 (fixed divider — always on when APLL1 is on)
 *   2. set_parent(A1SYS_HP_SEL, APLL1_D4) — mux index 1
 *   3. enable APLL1 tuner
 *   4. enable A1SYS (gate + timing)
 *
 * mt8188_apll1_disable sequence (reverse):
 *   1. disable A1SYS
 *   2. disable APLL1 tuner
 *   3. set_parent(A1SYS_HP_SEL, clk26m) — mux index 0
 *   4. disable CLK_TOP_APLL1_D4
 * -------------------------------------------------------------------------
 */
int mt8188_apll1_enable(struct mt8188_afe *afe)
{
	int ret;

	/* Step 1: enable APLL1_D4 (fixed divider — tolerant if stub) */
	ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_APLL1_D4);
	if (ret != 0) {
		return ret;
	}

	/* Step 2: set A1SYS_HP mux parent to apll1_d4 (index 1) + enable gate.
	 * A1SYS_HP is a dynamic-parent clock, so clock_control_on() enables the
	 * gate only and leaves the parent set above intact.
	 */
	a1sys_hp_set_parent(afe, 1);
	clock_control_on(afe->clk_topckgen, (clock_control_subsys_t)(uintptr_t)CLK_TOP_A1SYS_HP);

	/* Step 3: enable APLL1 tuner */
	mt8188_afe_tuner_enable(afe, &mt8188_afe_tuner_cfgs[MT8188_AUD_PLL1]);

	/* Step 4: enable A1SYS */
	mt8188_afe_enable_a1sys(afe);

	return 0;
}

int mt8188_apll1_disable(struct mt8188_afe *afe)
{
	/* Step 1: disable A1SYS */
	mt8188_afe_disable_a1sys(afe);

	/* Step 2: disable APLL1 tuner */
	mt8188_afe_tuner_disable(afe, &mt8188_afe_tuner_cfgs[MT8188_AUD_PLL1]);

	/* Step 3: disable gate, then revert A1SYS_HP mux to clk26m (index 0) */
	clock_control_off(afe->clk_topckgen, (clock_control_subsys_t)(uintptr_t)CLK_TOP_A1SYS_HP);
	a1sys_hp_set_parent(afe, 0);

	/* Step 4: disable APLL1_D4 */
	(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_APLL1_D4);

	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_apll2_enable / mt8188_apll2_disable
 * Linux: mt8188_apll2_enable() / mt8188_apll2_disable()
 *
 * mt8188_apll2_enable sequence:
 *   1. enable APLL tuner (PLL2)
 *   2. enable A2SYS (CLK_AUD_A2SYS + A2SYS_TIMING_ON)
 *
 * mt8188_apll2_disable sequence:
 *   1. disable A2SYS
 *   2. disable APLL tuner (PLL2)
 * -------------------------------------------------------------------------
 */
int mt8188_apll2_enable(struct mt8188_afe *afe)
{
	mt8188_afe_tuner_enable(afe, &mt8188_afe_tuner_cfgs[MT8188_AUD_PLL2]);
	mt8188_afe_enable_a2sys(afe);
	return 0;
}

int mt8188_apll2_disable(struct mt8188_afe *afe)
{
	mt8188_afe_disable_a2sys(afe);
	mt8188_afe_tuner_disable(afe, &mt8188_afe_tuner_cfgs[MT8188_AUD_PLL2]);
	return 0;
}

/* -------------------------------------------------------------------------
 * mt8188_afe_enable_reg_rw_clk / mt8188_afe_disable_reg_rw_clk
 * Linux: mt8188_afe_enable_reg_rw_clk() / mt8188_afe_disable_reg_rw_clk()
 *
 * Enables the minimum set of clocks required for AFE register access:
 *   1. CLK_TOP_AUDIO_LOCAL_BUS — bus clock for DRAM access
 *   2. CLK_TOP_AUD_INTBUS      — bus clock for AFE SRAM access
 *   3. CLK_ADSP_AUDIO_26M      — 26M reference
 *   4. CLK_AUD_AFE             — AFE HW gate
 *   5. CLK_AUD_A1SYS_HP        — A1SYS HP gate
 *   6. CLK_AUD_A1SYS           — A1SYS gate
 * -------------------------------------------------------------------------
 */
int mt8188_afe_enable_reg_rw_clk(struct mt8188_afe *afe)
{
	int ret;

	/* bus clock for AFE external access (DRAM) */
	ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_AUDIO_LOCAL_BUS);
	if (ret != 0) {
		return ret;
	}

	/* bus clock for AFE internal access (SRAM) */
	ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_AUD_INTBUS);
	if (ret != 0) {
		return ret;
	}

	/* audio 26M clock source */
	adsp_audio26m_enable(afe);

	/* AFE HW clock */
	mt8188_audsys_clk_on(afe->base, MT8188_CLK_AUD_AFE);
	mt8188_audsys_clk_on(afe->base, MT8188_CLK_AUD_A1SYS_HP);
	mt8188_audsys_clk_on(afe->base, MT8188_CLK_AUD_A1SYS);

	return 0;
}

int mt8188_afe_disable_reg_rw_clk(struct mt8188_afe *afe)
{
	mt8188_audsys_clk_off(afe->base, MT8188_CLK_AUD_A1SYS);
	mt8188_audsys_clk_off(afe->base, MT8188_CLK_AUD_A1SYS_HP);
	mt8188_audsys_clk_off(afe->base, MT8188_CLK_AUD_AFE);
	adsp_audio26m_disable(afe);
	(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_AUD_INTBUS);
	(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_AUDIO_LOCAL_BUS);

	return 0;
}

/* -------------------------------------------------------------------------
 * afe_enable_base_clocks / afe_disable_base_clocks
 *
 * Common base clocks enabled for all eTDM paths.
 * Calls mt8188_afe_enable_reg_rw_clk() + mt8188_afe_enable_main_clock(),
 * then additional eTDM-path clocks. A1SYS_TIMING_ON is NOT set here — it is
 * tied to APLL1 enable only (mt8188_apll1_enable), mirroring Linux.
 * -------------------------------------------------------------------------
 */
int afe_enable_base_clocks(struct mt8188_afe *afe)
{
	int ret;

	/* Core register-access clocks (includes the CLK_AUD_A1SYS gate) */
	ret = mt8188_afe_enable_reg_rw_clk(afe);
	if (ret) {
		return ret;
	}

	/* Main clock (26M timing + AFE_DAC_CON0).
	 * Note: A1SYS_TIMING_ON is deliberately NOT set here — like Linux, it
	 * is tied to APLL1 enable only (mt8188_apll1_enable via the rate-based
	 * domain), so a2sys-only (44.1k) paths don't assert a1sys timing.
	 */
	mt8188_afe_enable_main_clock(afe);

	/* --- Additional clocks for eTDM paths (dependent on bus clocks above) --- */

	/* INFRA audio gates — required for I2S DMA and audio 26M BCLK */
	ret = clk_on_tolerant(afe->clk_infra_ao, CLK_INFRA_AO_AUDIO);
	if (ret != 0) {
		return ret;
	}
	ret = clk_on_tolerant(afe->clk_infra_ao, CLK_INFRA_AO_AUDIO_26M_BCLK);
	if (ret != 0) {
		return ret;
	}
	ret = clk_on_tolerant(afe->clk_infra_ao, CLK_INFRA_AO_I2S_DMA);
	if (ret != 0) {
		return ret;
	}

	/* Audio H — required for hi-res DAC/ADC and DMIC */
	ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_AUDIO_H);
	if (ret != 0) {
		return ret;
	}

	/* ASM clocks — required for GASRC and audio stream processing */
	ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_ASM_H);
	if (ret != 0) {
		return ret;
	}
	ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_ASM_L);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

int afe_disable_base_clocks(struct mt8188_afe *afe)
{
	/* Additional clocks off first (reverse of enable order) */
	(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_ASM_L);
	(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_ASM_H);
	(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_AUDIO_H);

	(void)clk_off_tolerant(afe->clk_infra_ao, CLK_INFRA_AO_I2S_DMA);
	(void)clk_off_tolerant(afe->clk_infra_ao, CLK_INFRA_AO_AUDIO_26M_BCLK);
	(void)clk_off_tolerant(afe->clk_infra_ao, CLK_INFRA_AO_AUDIO);

	/* Main clock */
	mt8188_afe_disable_main_clock(afe);

	/* A1SYS_TIMING_ON is owned by mt8188_apll1_disable (rate-based domain),
	 * not cleared here — mirrors Linux.
	 */

	/* Core register-access clocks (gates the CLK_AUD_A1SYS gate) */
	mt8188_afe_disable_reg_rw_clk(afe);

	return 0;
}

/* -------------------------------------------------------------------------
 * APLL domain selection by sample rate — Linux mt8188_get_apll_by_rate().
 *   rate % 8000 == 0 (48k family)  → APLL1 / a1sys
 *   else            (44.1k family) → APLL2 / a2sys
 * The a1sys_hp / a2sys muxes are fixed to apll1_d4 / apll2_d4 respectively,
 * so the timing domain *is* the APLL choice. Used by both the eTDM OUT and
 * IN clock paths — the domain follows the sample rate, not the port.
 * -------------------------------------------------------------------------
 */
int mt8188_afe_apll_by_rate(uint32_t rate)
{
	return ((rate % 8000U) == 0U) ? MT8188_APLL1 : MT8188_APLL2;
}

/*
 * The timing domain a sample rate needs.
 *
 * Reference counted, because a domain is shared in both directions: one stream
 * holds a single domain even when it drives two eTDM ports (UL9 over IN1+IN2,
 * DL11 32ch over OUT1+OUT2), and two streams of the same rate family hold the
 * same domain. Without the count, stopping either of two concurrent 48 kHz
 * streams would tear down APLL1, its tuner and a1sys under the other one -
 * which the shipped samples do not catch, because they stop both streams at
 * teardown and so never leave one running alone.
 *
 * The counts are the only shared state here; the clock_control drivers
 * underneath do not count, so a gate goes off the moment it is asked to.
 */
int mt8188_afe_enable_apll_domain(struct mt8188_afe *afe, uint32_t rate)
{
	int idx = mt8188_afe_apll_by_rate(rate);
	k_spinlock_key_t key;
	bool first;
	int ret;

	key = k_spin_lock(&afe->lock);
	first = (afe->apll_users[idx] == 0U);
	if (afe->apll_users[idx] == UINT8_MAX) {
		k_spin_unlock(&afe->lock, key);
		return -EOVERFLOW;
	}
	afe->apll_users[idx]++;
	k_spin_unlock(&afe->lock, key);

	if (!first) {
		return 0;
	}

	if (idx == MT8188_APLL1) {
		/* Root PLL, its divider, the a1sys reparent, tuner and gate.
		 * mt8188_apll1_enable() subsumes the A1SYS_HP gate.
		 */
		ret = clk_on_tolerant(afe->clk_apmixed, CLK_APMIXED_APLL1);
		if (ret != 0) {
			return ret;
		}
		mt8188_apll1_enable(afe);
		ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_APLL1);
		if (ret != 0) {
			return ret;
		}
	} else {
		/* Root PLL, tuner, and a2sys gate plus its timing bit. */
		ret = clk_on_tolerant(afe->clk_apmixed, CLK_APMIXED_APLL2);
		if (ret != 0) {
			return ret;
		}
		mt8188_apll2_enable(afe);
		ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_APLL2);
		if (ret != 0) {
			return ret;
		}
		ret = clk_on_tolerant(afe->clk_topckgen, CLK_TOP_A2SYS);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int mt8188_afe_disable_apll_domain(struct mt8188_afe *afe, uint32_t rate)
{
	int idx = mt8188_afe_apll_by_rate(rate);
	k_spinlock_key_t key;
	bool last;

	key = k_spin_lock(&afe->lock);
	if (afe->apll_users[idx] == 0U) {
		k_spin_unlock(&afe->lock, key);
		return -EALREADY;
	}
	afe->apll_users[idx]--;
	last = (afe->apll_users[idx] == 0U);
	k_spin_unlock(&afe->lock, key);

	if (!last) {
		return 0;
	}

	if (idx == MT8188_APLL1) {
		(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_APLL1);
		mt8188_apll1_disable(afe);
		(void)clk_off_tolerant(afe->clk_apmixed, CLK_APMIXED_APLL1);
	} else {
		(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_A2SYS);
		(void)clk_off_tolerant(afe->clk_topckgen, CLK_TOP_APLL2);
		mt8188_apll2_disable(afe);
		(void)clk_off_tolerant(afe->clk_apmixed, CLK_APMIXED_APLL2);
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Per-eTDM-port clocks.
 *
 * Each eTDM port owns exactly ONE APLL12 MCLK divider, one I2Sx MCLK mux and
 * one AUDSYS gate. The mapping matches Linux
 * mtk_dai_etdm_get_clkdiv_id_by_dai_id() / _get_clk_id_by_dai_id() /
 * _get_cg_id_by_dai_id(). Expressing it as a table means a port can never
 * enable — or, on stop, disable — another port's divider, which would break a
 * concurrently running stream on that other port.
 *
 * The rate-selected APLL timing domain is deliberately NOT handled here: a
 * stream has one domain but may drive two ports (UL9 = IN1+IN2, DL11 32ch =
 * OUT1+OUT2). See mt8188_afe_enable_apll_domain(), called once by start().
 * -------------------------------------------------------------------------
 */
struct etdm_port_clks {
	uint32_t div_clk;    /* CLK_TOP_APLL12_CK_DIVx — this port's MCLK divider */
	uint32_t mux_clk;    /* CLK_TOP_I2SIx / I2SOx  — this port's MCLK mux */
	uint8_t audsys_gate; /* MT8188_CLK_AUD_ETDM_xxx */
};

static const struct etdm_port_clks etdm_port_clk_table[MT8188_ETDM_NR] = {
	[MT8188_ETDM_OUT1] = {CLK_TOP_APLL12_CK_DIV2, CLK_TOP_I2SO1, MT8188_CLK_AUD_ETDM_OUT1},
	[MT8188_ETDM_OUT2] = {CLK_TOP_APLL12_CK_DIV3, CLK_TOP_I2SO2, MT8188_CLK_AUD_ETDM_OUT2},
	[MT8188_ETDM_IN1] = {CLK_TOP_APLL12_CK_DIV0, CLK_TOP_I2SI1, MT8188_CLK_AUD_ETDM_IN1},
	[MT8188_ETDM_IN2] = {CLK_TOP_APLL12_CK_DIV1, CLK_TOP_I2SI2, MT8188_CLK_AUD_ETDM_IN2},
};

int mt8188_afe_enable_etdm_clocks(struct mt8188_afe *afe, enum mt8188_etdm_id id)
{
	const struct etdm_port_clks *p;
	int ret;

	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	p = &etdm_port_clk_table[id];

	ret = clk_on_tolerant(afe->clk_topckgen, p->div_clk);
	if (ret != 0) {
		return ret;
	}
	ret = clk_on_tolerant(afe->clk_topckgen, p->mux_clk);
	if (ret != 0) {
		return ret;
	}
	mt8188_audsys_clk_on(afe->base, (enum mt8188_audsys_clk_id)p->audsys_gate);

	return 0;
}

int mt8188_afe_disable_etdm_clocks(struct mt8188_afe *afe, enum mt8188_etdm_id id)
{
	const struct etdm_port_clks *p;

	if ((unsigned int)id >= MT8188_ETDM_NR) {
		return -EINVAL;
	}
	p = &etdm_port_clk_table[id];

	/* Reverse order of enable. Base clocks stay on for the device lifetime
	 * (see mt8188_afe_init); the APLL domain is released by the caller.
	 */
	mt8188_audsys_clk_off(afe->base, (enum mt8188_audsys_clk_id)p->audsys_gate);
	(void)clk_off_tolerant(afe->clk_topckgen, p->mux_clk);
	(void)clk_off_tolerant(afe->clk_topckgen, p->div_clk);

	return 0;
}
