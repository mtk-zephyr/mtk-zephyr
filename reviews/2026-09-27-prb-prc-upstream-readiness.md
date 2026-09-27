# Upstream readiness of PR B and PR C — review-agent, 2026-09-27

Written by **review-agent**, a Claude reviewer separate from the authoring side and the build
machine. It read the code, built it and ran compliance; it did not run anything on hardware and
did not change any code branch.

Shareable page with the same content: https://claude.ai/artifact/1X8vCXtyhZjMcSsiJWGXuo
(private until Aary shares it).

| | |
|---|---|
| Reviewed | the 12 commits after PR A on `mtk-genio-dev`, tip `978440f69b6` |
| PR B | `024e1101f6f` … `a3a352da5ce`, six commits: MT8188 GPIO and EINT |
| PR C | `7fd88e973ba` … `ac0fbd3d9d8`, five commits: audio clocks, AFE driver, snippet (+ `978440f69b6`, see B6) |
| Code links | `L<n>` references are line numbers at `978440f69b6` |

Findings carry IDs (B1, C5, …) so they can be referred to in discussion.

## Verdict

- **PR B: close.** Hold it for one platform decision (B1) and two driver fixes (B2, B3). The rest
  is review polish.
- **PR C: not ready.** Four structural blockers (C1–C4: licence, CI coverage, API shape,
  hard-coded hardware) and four real bugs (C5–C8).

## How this was checked

| Check | Result |
|---|---|
| Rebase | All 12 commits cherry-picked onto upstream `main` `3e7672a71bd` (PR A merged). No conflicts. |
| Builds | Both boards: plain `hello_world`, `-S mtk-afe`, `tests/drivers/gpio/gpio_basic_api`, plus AFE and GPIO builds with `-Werror` and asserts. **8/8 pass, 0 warnings.** |
| Compliance | `check_compliance.py -c main..HEAD` with `GENIO: ` stripped: **0 failures, 0 warnings.** DevicetreeLinting skipped (dts-linter not installed). |
| Linux cross-check | MT8188 pin table, `mtk-eint.c`, `clk-mt8188-topckgen.c`, `mt8188-afe-clk.c`, Genio 700/510 device trees. |
| Not done | Per-commit build sweep on the new base; any hardware run. Items marked *not reproduced* are reasoned from code. |

## PR B — GPIO and EINT

### B1 · Risk · Zephyr takes the whole EINT block from Linux but handles only 177 of its 225 lines

The EINT controller is one register block with one GIC interrupt, SPI 235, and Linux uses it
too. Upstream Linux's Genio device tree (`mt8390-genio-common.dtsi`, included by both EVKs) puts
the PMIC on EINT 222, the touchscreen on 6 and the Type-C controller on 12.

The SoC dtsi enables the Zephyr `eint` node by default. Its init masks lines 0–176, which
silences Linux's EINT users. `num-lines = <177>` leaves lines 177–224 unmasked and never
scanned; Linux declares 225 (`mt8188_eint_hw.ap_num`). H9 passing under the plain cell shows
SPI 235 reaches Zephyr. If the PMIC asserts line 222 while Zephyr runs, the level-triggered SPI
stays high and nothing clears it, so the Zephyr core should livelock in the ISR.

*Not reproduced.* The boards run MediaTek's own kernel, whose device tree may differ from
upstream. Test: press the power key or trigger an RTC alarm while the GPIO test image runs.

- **Fix:** set `num-lines` to 225 so the binding describes the hardware. Decide which cell owns
  EINT; it cannot be split by line. Consider `status = "disabled"` in the SoC dtsi so only boards
  that need GPIO interrupts take it. The board docs should say GPIO interrupts take EINT away
  from Linux.
- **Where:** `dts/arm64/mediatek/mt8188.dtsi` L97; `drivers/interrupt_controller/intc_mt8188_eint.c`
  L270–281 (ISR), L316–328 (init).

### B2 · Bug · Both-edge emulation can drop an edge

`gpio_mt8188_arm_dual_edge()` re-arms through `eint_mtk_set_trigger()`, which always
acknowledges the line. An edge that latches while the re-arm loop runs is cleared, and the loop
re-arms past it without a callback. Linux flips the polarity without acknowledging and re-raises
the event through `SOFT_SET` when the level moved during the flip. The bench tests toggle
slowly, so they cannot hit this window.

- **Fix:** a polarity-only flip in the EINT interface that does not acknowledge, plus re-raising
  the event (SOFT_SET at 0x240, or a synthesized callback) when the level changed during re-arm.
- **Where:** `drivers/gpio/gpio_mt8188.c` L95–114; `intc_mt8188_eint.c` L227.

### B3 · Gap · The domain-enable register is never written

Linux's `mtk_eint_hw_init()` sets `DOM_EN` (offset 0x400) for every line before use. The Zephyr
driver never writes it and works only because Linux booted first. Zephyr on this SoC without
Linux would see no EINT events.

- **Fix:** write `DOM_EN` in init, or document the dependency in the binding.
- **Where:** `intc_mt8188_eint.c` L301–333.

### B4 · Design · Level-triggered interrupts are refused

The controller supports level detection. Zephyr's convention is that a level callback disables
the pin interrupt or clears the source, and the EINT ISR already acknowledges before dispatch,
which fits it. 21 in-tree sensor, input and network drivers request `GPIO_INT_LEVEL_*` and would
get `-ENOTSUP` on these boards. Expect reviewers to question the commit message's reasoning.

- **Fix:** support level triggers and document the callback's responsibility.
- **Where:** `gpio_mt8188.c` L246–256.

### B5 · Design · The EINT consumer interface is a set of global functions

`include/zephyr/drivers/interrupt_controller/intc_mtk_eint.h` presents a MediaTek-wide
interface, but it is implemented as non-static functions inside the MT8188 driver, with no
device API table and no check that `dev` is an EINT device. A second MediaTek EINT driver (MT8365
in PR D) could not link alongside it. Smaller: `num-lines` is a custom property without a vendor
prefix, and `eint_mtk_callback_t` is a new struct typedef.

- **Fix:** a `__subsystem` API with `DEVICE_API`, or rename the interface to be MT8188-specific;
  rename the property to `mediatek,num-lines`.

### B6 · Packaging · One GPIO commit sits in the AFE group

`978440f69b6` documents where GPIO 38 and 40 reach the header but sits after the six AFE
commits. Squash it into `a3a352da5ce` so PR B is self-contained.

## PR C — Audio Front End

### C1 · Blocker · GPL-2.0 register header compiled into the firmware

`drivers/audio/mt8188_afe/mt8188_afe_reg.h` is the Linux header, GPL-2.0 only, recorded in
`REUSE.toml` as unresolved. It defines 3,059 macros; the driver's `.c` files reference about 249
of them directly. Separately, six source files say "Ported from Linux" in their headers while
carrying Apache-2.0 SPDX lines: `mt8188_afe.c`, `mt8188_afe_clk.c`, `mt8188_afe_etdm.c`,
`mt8188_afe_audsys.c`, `clock_control_mt8188_apmixed.c`, `clock_control_mt8188_topckgen.c`.

- **Fix:** write a small Apache-2.0 register header from the datasheet covering only what the
  driver uses; no relicensing decision needed. For the ported files, state in the commit message
  that MediaTek holds the copyright on the originals and contributes them under Apache-2.0, or
  describe the source as the datasheet if that is accurate.

### C2 · Blocker · CI never compiles the AFE driver

The driver is enabled only by the `mtk-afe` snippet, and nothing in `tests/` or `samples/` uses
the snippet. Moving the ten samples to `mtk-zephyr/samples` removed the only build path, so about
8,000 lines would merge without a single CI build.

- **Fix:** one in-tree sample (a loopback is the most useful) or a build-only test that applies
  the snippet.

### C3 · Blocker · A new vendor-only driver API, with a route() call that carries no information

The series adds `__subsystem struct mt8188_afe_driver_api` under `include/zephyr/drivers/audio/`.
Expect the audio and I2S maintainers to ask for the I2S API.

The public `route()` call weakens the case for a custom API. The seven legal routes follow
entirely from the memory interface and the channel count, which `configure()` already derives
(`memif_to_etdm()`, `memif_second_etdm()`); the driver's own comment says the topology is fixed.
`route()` also created the ordering hazard behind the 2026-09-23 hang.

- **Fix:** program the matrix inside `configure()` or `start()` and drop `route()` from the API.
  With routing internal, each memory interface behaves like an I2S stream, so evaluate the I2S API
  before submitting.
- **Where:** `include/zephyr/drivers/audio/mt8188_afe.h` L177–189; `mt8188_afe.c` L1121–1145,
  L1292–1324.

### C4 · Blocker · Hard-coded addresses and other drivers' registers

The driver maps TOPRGU at `0x10007000` to pulse the audiosys reset through `WDT_SWSYSRST`, and the
audio 26 MHz gate at `0x10b91100`, both by literal address. It raises AXI bus protection by
writing INFRACFG_AO registers through `DEVICE_MMIO_GET(clk_infra_ao)`, and writes the APLL12
master-clock divider (TOPCKGEN `0x328`) through `DEVICE_MMIO_GET(clk_topckgen)`.

- **Fix:** describe these in devicetree: a reset controller referenced with `resets`, a syscon
  for the gate, and the topckgen driver owning its dividers through a rate API.
- **Where:** `mt8188_afe.c` L1888–1911, L1948; `mt8188_afe_etdm.c` L990.

### C5 · Bug · Two owners for the audio PLL tuner enable bit

The tuner enable bit (`AFE_APLL_TUNER_CFG` / `CFG1`, bit 0) is toggled by the reference-counted
domain code in `mt8188_afe_clk.c` and by the per-port master-clock code in `mt8188_afe_etdm.c`,
which clears it on every stop with no count. Stopping DL8 while DL11 runs on the same PLL, both
with a master clock, turns the tuner off under DL11. Linux reference-counts the tuner (`ref_cnt`
under `ctrl_lock` in `mt8188-afe-clk.c`). The tuner tables are also duplicated between the two
files.

- **Fix:** one reference-counted tuner implementation, owned by the clock code.
- **Where:** `mt8188_afe_etdm.c` L1032; `mt8188_afe_clk.c` L255–302.

### C6 · Bug · Domain reference count and API calls are not serialized

`mt8188_afe_enable_apll_domain()` increments the count, drops the lock, then enables the clocks.
If enabling fails the count is never returned, and the next caller skips the enable, so `route()`
touches the a1sys registers with the domain down: the hang fixed on 2026-09-23. A second thread
arriving before the first finishes also returns early. More generally, `start()` tests `started`
without the lock and `configure()` writes shared state unlocked.

- **Fix:** one `k_mutex` per device held across each API call; return the count on the enable
  error path.
- **Where:** `mt8188_afe_clk.c` L620–671; `mt8188_afe.c` L1353–1358.

### C7 · Bug · Period callbacks can never fire

All five active memory interfaces (DL8, DL11, UL3, UL8, UL9) have `irq_id = -1`, so
`set_period_cb()`, `period_frames`, the ISR and the 19-entry IRQ table do nothing on every
supported path. The public header documents the callback as working.

- **Fix:** wire the interrupts, or have `set_period_cb()` return `-ENOTSUP` and remove
  `period_frames` until they are.
- **Where:** `mt8188_afe.c` L426–432, L1733–1752.

### C8 · Bug · The PLL driver never sets a frequency

`clock_control_mt8188_apmixed.c` powers PLLs on and off but never writes PCW or the post-divider;
those table fields are unused, although the commit message says enabling sets the divider. The
AFE hard-codes 196.608 MHz and 180.6336 MHz, which holds only because Linux programmed the PLLs.
The driver also exposes on/off for all 15 PLLs, including MAINPLL and UNIVPLL, which one wrong
clock specifier could turn off.

- **Fix:** program the audio PLL rates (or implement `get_rate` and check it) and trim the table
  to APLL1 and APLL2.
- **Where:** `clock_control_mt8188_apmixed.c` L397–442.

### Minor

| ID | Finding |
|---|---|
| C9 | `CLOCK_CONTROL_INIT_PRIORITY` forced to 1 for the whole SoC (`Kconfig.defconfig.mt8188_a55` L16–19). The stated reason is the AFE, which initializes at `POST_KERNEL`. A `-S mtk-afe` build with the default 30 and `CONFIG_CHECK_INIT_PRIORITIES=y` passes. Drop it or state the real dependency. |
| C10 | `clk_on_tolerant()` treats `-ENOTSUP` as success for every clock. The devicetree lists `CLK_TOP_APLL1_D4` and `APLL2_D4`, which topckgen does not implement, so a wrong clock ID fails silently. Make the fixed dividers explicit no-ops in topckgen. |
| C11 | No logging in any of the four drivers, while every failure found on hardware was silent. Add `LOG_ERR` on init failures. AFE init could write `AFE_IRQ_MASK` and read it back to detect a powered-down audio domain and fail with `-EIO`. |
| C12 | Board docs omit the Linux-side audio prerequisites (the `power/control` write, eTDM pin muxing); only the samples repository documents them. |
| C13 | `snippets/mtk-afe` has no `README.rst` (only one other in-tree snippet lacks one); vendor snippets now live under `snippets/<vendor>/`. |
| C14 | Comments that contradict the code: topckgen gives the `MUX_GATE_CLR_SET_UPD` argument order two ways (L51–56 vs L115–121) and the `audio_local_bus` parent as 2 (L39) and 4 (L131); `mt8188_afe_audsys.h` L31–34 gate bits disagree with the table. The code is right; parent indices were checked against Linux. |
| C15 | `mt8188_afe_etdm.c` L36–45 defines `FIELD_PREP`/`FIELD_GET` saying Zephyr lacks them; they are in `include/zephyr/sys/util_macro.h`. |
| C16 | Comments narrate debugging history ("the pre-fix behaviour", "which the shipped samples do not catch"), plus about 125 "Linux:" cross-references. Upstream `AGENTS.md` asks for comments that describe the tree as it is, tersely. |
| C17 | `apmixedsys` and `topckgen` nodes have no `status`, so both drivers build and initialize in the plain image, where the ordinary cell grants neither block. Harmless today (init only maps). Consider `status = "disabled"` plus enabling from the snippet. |

## Questions for the team

| ID | Question |
|---|---|
| Q1 | Who owns the EINT block on Genio while Zephyr runs, and is Linux expected to give up its EINT users? (B1) |
| Q2 | Is MediaTek committed to the custom AFE API, or open to an I2S-based driver with routing handled inside the driver? (C3) |
| Q3 | For the register header: relicense it, or rewrite the roughly 250 definitions the driver uses? (C1) |
| Q4 | What is `CLOCK_CONTROL_INIT_PRIORITY = 1` actually for? (C9) |
| Q5 | Upstream `doc/contribute/guidelines.rst` asks for an `Assisted-by:` tag when AI tools helped write a contribution. None of the 12 commits carries one. Decide before submission. |

## Suggested order of work

**PR B:** squash `978440f69b6` into `a3a352da5ce` (B6) → fix B2 and B3 → decide Q1 and set
`num-lines` to 225 (B1) → address B4 and B5 → submit.

**PR C:** C1 and C2 first (mechanical, unblock review) → decide Q2 before changing the API (C3)
→ C4 through C8 → minor items in the same pass.

## Checked and correct

- GPIO *n* maps to EINT line *n* for pins 0–176, matching Linux's MT8188 pin table. Pins 177–189
  map to lines 212–224 and are not used by these boards.
- The pin controller maps its registers at `PRE_KERNEL_1` priority 0, before the GPIO banks apply
  their pin states at 41.
- Every fixed topckgen parent index (AUD_INTBUS, AUDIO_H, AUDIO_LOCAL_BUS, ASM_H/L, APLL1/2,
  AUD_IEC, A2SYS) matches Linux's parent lists.
- `set_buf()` and `configure()` reject malformed buffers and stream shapes before any register
  write; `start()` unwinds in reverse on failure.
- The 12 commits apply to current upstream `main` without conflicts, build clean with `-Werror`,
  and pass compliance.

## Already tracked in STATUS.md, not repeated here

The snippet's `pinctrl-0` applying nothing under the hypervisor, the four rpmsg cells granting
2 MB, the dropped `*_mtk_common.c` split, and credit for the authors of the original MediaTek
drivers.
