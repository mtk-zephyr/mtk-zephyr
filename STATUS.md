# STATUS — as of 2026-09-02

Current state of the MediaTek Genio work on `github.com/mtk-zephyr/mtk-zephyr`. This file is
overwritten on every update; `git log` on this branch is the history.

## Branch tips

| Branch | Tip | Contents | Verified |
|---|---|---|---|
| `main` | `5a56224939a` | upstream Zephyr mirror, no MediaTek work | n/a |
| `mtk-genio-dev` | `bfaca73809d` | 19 commits (drop 2026-09-02) | builds + compliance, **no hardware** |
| `mtk-v4.4.2` | `c4333dd7d9c` | 18 commits on the `v4.4.2` release tag | builds + compliance, **no hardware** |

Tag `verified/2026-09-02` pins the tested `mtk-genio-dev` SHA.

## What is verified on `mtk-genio-dev` (`bfaca73809d`)

| Gate | Result |
|---|---|
| `git am` of all 19 patches onto `origin/main` | clean, no conflicts |
| `mt8390_genio_700_evk/mt8188/a55` | PASS — RAM 144 KB / 2 MB |
| `mt8370_genio_510_evk/mt8188/a55` | PASS — RAM 144 KB / 2 MB |
| `west boards \| grep genio` | both boards listed |
| 5 ADSP targets (mt8186/8188/8195/8196/8365) | PASS on branch and on base |
| ADSP behaviour neutrality | 0 config removals, **loadable binaries byte-identical** to base, all five |
| `checkpatch.pl -g origin/main..HEAD` | 0 errors, 0 warnings across all 19 |
| `get_maintainer.py` on both `board.yml` | resolves to MediaTek Platforms / aary-p |
| Subject lengths | all within 75 (longest exactly 75) |
| Author / committer | `Aary Patil <aary.patil@mediatek.com>` on all 19 |

## What is NOT verified — read before writing any PR description

**Hardware: nothing at all.** The board is not connected yet. No boot has ever been performed on
either Genio 700 or Genio 510, on either branch. Two changes have failure modes that **no build can
detect**:

- **`IRQ_TYPE_LEVEL` in the MT8188 dtsi.** Zephyr's GIC flags cell encodes only level-vs-edge, and
  Linux's `IRQ_TYPE_LEVEL_HIGH` shares a value with `IRQ_TYPE_EDGE`. Wrong here means the arch-timer
  PPIs are edge-triggered — visible only as inaccurate `k_sleep`, never as a compile error.
- **The GIC MMU restore on `mtk-v4.4.2`.** Its failure mode is a silent fault during interrupt setup,
  before the console exists. No output, no clue. Code-reviewed and corroborated against every other
  arm64 SoC on that branch, but never executed.

**`cntfrq` unconfirmed.** `Kconfig.defconfig.mt8188_a55` sets `SYS_CLOCK_HW_CYCLES_PER_SEC =
13000000` as a fallback. Needs `printk("cntfrq = %u\n", sys_clock_hw_cycles_per_sec());` on a booted
board. If it is not 13 MHz, the a55 cpucluster commit needs amending.

**License and copyright unverified.** `LicenseAndCopyrightCheck` **crashes** on this machine rather
than reporting — `reuse` 6.2.0 vs `scripts/list_undocumented_licenses.py`. It crashes identically on
a bare upstream commit, so it is environmental. See `artifacts/license-check-crash.txt`. When a
report here says "compliance passes", license/copyright is not among the things that passed.

## Open findings

| # | Finding | State |
|---|---|---|
| 1 | `MmuRegionsCheck` warns on the PINCTRL entry in `mt8188/a55/mmu_regions.c`. The checker flags every added `MT_DEVICE` entry unconditionally — a comment does **not** silence it, despite the message saying so. Clearing it requires converting `pinctrl_mt8188.c` to the device MMIO API; the static mapping is currently load-bearing. | open, warning severity only |
| 2 | 28 `TODO(` markers block PR A — 13 in `mt8390_genio_700_evk/doc/index.rst`, 15 in `mt8370_genio_510_evk/doc/index.rst`. Facts only MediaTek can supply. Not invented, per instruction. | blocking PR A |
| 3 | `mtk-v4.4.2` is behind the 2026-09-02 drop: 4 A55 cores instead of 6, no Genio 510 board, no board docs. Unclear whether deliberate freeze or oversight. | awaiting decision |
| 4 | `reuse` 6.2.0 breaks the license check. Fix: `pip install 'reuse<6.2.0'`. | environmental, needs Aary |

## Environment notes that affect how results should be read

- **The west workspace tracks one branch's manifest at a time.** Building a branch without a
  matching `west update` produces phantom failures in unrelated modules. Every build result recorded
  here came from a matched workspace.
- **`check_compliance.py` defaults to `HEAD~1..HEAD`** — one commit. All runs recorded here pass
  `-c <base>..HEAD` explicitly.
- **Interactive git flags are unavailable** on the build machine. `git rebase -i`, including
  `--autosquash`, cannot be used. Fixes are folded into commits by detach → amend → cherry-pick
  replay instead.
- Toolchain: `clang-format` 22.1.0, Zephyr SDK 1.0.1 (aarch64 + Xtensa).

## The two branches differ on purpose

Four files are deliberately **not** identical, because `mtk-v4.4.2` sits on a release tag predating
several upstream changes. A file being identical on both is not reassurance — it may be the bug.

| File | `mtk-genio-dev` | `mtk-v4.4.2` |
|---|---|---|
| `drivers/serial/uart_mtk_common.{c,h}` | `void uart_mtk_irq_update()` | `int`, `return 1` |
| `soc/mediatek/mt8xxx/Kconfig` | `SOC_MTK_ADSP` selects `CPU_HAS_DCACHE`, `ARCH_HAS_NOCACHE_MEMORY_SUPPORT` | neither select exists |
| `soc/mediatek/mt8xxx/Kconfig.defconfig` | `configdefault DCACHE` | `config DCACHE` / `default y` |
| `soc/mediatek/mt8xxx/mt8188/a55/mmu_regions.c` | pin controller only (arch core maps the GIC) | pin controller **plus both GIC banks** |
