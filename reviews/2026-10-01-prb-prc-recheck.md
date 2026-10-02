# Re-check of PR B and PR C — review-agent, 2026-10-01

Follow-up to `2026-09-27-prb-prc-upstream-readiness.md`; finding IDs are unchanged. Re-checked at
`mtk-genio-dev` **`9752f4c7b6c`** (11 commits on upstream `3c61a12bdd8`) against the 27 Sep tip
`978440f69b6`. Shareable page, updated in place: https://claude.ai/artifact/1X8vCXtyhZjMcSsiJWGXuo
(private until Aary shares it).

## Verdict

- **PR B: nearly ready.** EINT ownership is settled and implemented, and CI now builds the driver
  through `gpio_basic_api`. Fix B2 and B3 (both small); expect review to raise B4 and B5.
- **PR C: not ready.** The GPL notice is gone and C7 and C9 are fixed. Still blocking: no CI
  build (C2), hard-coded addresses (C4), and how the relicensing is stated (C1). Bugs C5 and C8
  are open and C6 is half-fixed.

## How it was checked

| Check | Result |
|---|---|
| Diff | Old and new series compared per commit. Byte-identical: GPIO driver, eTDM file, both clock-controller drivers. Changed: EINT driver/binding/dtsi, AFE core and clock code, register header, boards, test overlays. |
| Builds | Both boards: plain `hello_world`, `-S mtk-afe`, `gpio_basic_api`, plus AFE and GPIO with `-Werror` and asserts. **8/8 pass, 0 warnings.** Workspace modules are 16 revisions older than the new base; no build was affected. |
| Compliance | Series with `GENIO: ` stripped: **34 checks, 0 failures, 0 warnings**. DevicetreeLinting skipped (dts-linter not installed). |
| CI coverage | `twister --dry-run` for the Genio 700 selects `drivers.gpio.2pin`; nothing that enables the AFE is selected. |
| Not done | Per-commit sweep; hardware (dev-agent reports 11/11 on the Genio 700 and all five AFE tests at this tip). |

## Status of every finding

| ID | Status | Note |
|---|---|---|
| B1 | **Fixed** | Shared line by line per the team's decision: `num-lines` 225, init leaves lines as found, the ISR masks lines it did not enable. Safety under the hypervisor rests on the mediator routing SPI 235 to the owning cell. |
| B2 | Open | GPIO driver unchanged; `set_trigger()` still acknowledges (`intc_mt8188_eint.c` L246). |
| B3 | Open | `DOM_EN` still never written. Setting it per line in `eint_mtk_enable()` fits the sharing model. |
| B4 | Open | Level triggers still refused; the board docs now say "edge only". |
| B5 | Open (rename withdrawn) | Plain `num-lines` is used by four other in-tree interrupt-controller bindings and 64 devicetree files, as the team said. The global-function API point stands. |
| B6 | **Fixed** | Header-pin commit dropped; the content lives in `mtk-zephyr/samples`. |
| B7 | **New**, minor | The EINT binding now describes the Genio hypervisor's sharing. Bindings describe hardware; move that to the board docs. |
| C1 | Partly fixed | See below. |
| C2 | Open | No in-tree build of the AFE. Copy the `gpio_basic_api` pattern: one audio sample or build-only test with a Genio overlay or the snippet. |
| C3 | Team decision | Custom API and `route()` kept. The PR description should give a concrete multi-destination example, since audio maintainers may still ask about I2S. |
| C4 | Open | TOPRGU `0x10007000` and `0x10b91100` still mapped by address; other drivers' registers still written directly. |
| C5 | Open | eTDM file unchanged. C8 (`twostream_dl11_dl8`) runs exactly this case, both streams with a master clock, and passes because it only checks the DMA pointer; reading `AFE_APLL_TUNER_CFG` bit 0 after its second phase would show the tuner off. |
| C6 | Partly fixed | Domain count now correct under a `k_mutex`. `start()` still tests `started` unlocked (two concurrent starts take the domain twice), and `configure()` still writes shared state unlocked. |
| C7 | **Fixed** | `set_period_cb()` returns `-ENOTSUP`; header documents polling. ISR and IRQ table remain unused. |
| C8 | Open | `clock_control_mt8188_apmixed.c` unchanged. |
| C9 | **Fixed** | Override removed. |
| C10–C17 | Open | Unchanged; C16 has one comment trimmed. |
| C18 | **New**, minor | `apll_domain_on()` returns at the first failure without undoing earlier steps, leaving the PLL on with a count of 0. |
| C19 | **New**, minor | The ISR comment says a period callback may reconfigure its stream, but `start()`/`stop()`/`route()` now take a `k_mutex`, which ISRs cannot. Becomes a bug when interrupts are wired. |

## C1 in detail

The header now reads `Copyright (c) 2026 MediaTek Inc.`, Apache-2.0, and its `REUSE.toml` entry
is gone, per Option A.

- **Provenance wording.** The AFE commit message says the header is "generated from the AFE
  register map". Its content matches Linux's `mt8188-reg.h` in 3,054 of 3,058 definitions,
  including a macro Linux has since fixed (`PWR2_TOP_CON1_DMIC_FIFO_SOFT_RST_EN(x)`, L2853, with
  an unparenthesized argument). A reviewer diffing it against Linux will read "generated" as a
  relabelled GPL file. All three Linux commits to that file are from MediaTek, so the
  relicensing is sound; the commit message should say so plainly, and the copyright line should
  keep the original year (2022–2026).
- **Ported driver code.** Option A covers MediaTek's own copyright. The seven Linux files the
  drivers cite also carry commits from outside MediaTek: `clk-pll.c` 20 of 40,
  `clk-mt8188-topckgen.c` 9 of 11, `clk-mt8188-apmixedsys.c` 6 of 7, `mt8188-afe-pcm.c` 9 of 17,
  `mt8188-dai-etdm.c` 3 of 10, `mt8188-afe-clk.c` 3 of 7, `mt8188-audsys-clk.c` 3 of 5. Many are
  likely tree-wide API changes a port would not carry, but MediaTek's open-source office should
  confirm before the grant is said to cover the ported logic. No AFE or clock commit message
  states the grant today.

## Questions

| ID | Status |
|---|---|
| Q1–Q4 | Answered 1 Oct (B1, C3, C1, C9). |
| Q5 | Open. Upstream guidelines ask for `Assisted-by:` when AI tools helped write a contribution; none of the 11 commits carries one, and STATUS now says the GPIO and EINT code "is generated, not written by a person". |
| Q6 | **New.** Has MediaTek's open-source office confirmed that Option A covers the ported driver logic (C1)? |

## Suggested order

**PR B:** B2, B3 → decide or prepare answers for B4, B5 → B7 → Q5 → submit.

**PR C:** C1 wording and Q6 → C2, C4 → C5, C8, API half of C6 → minor items.
