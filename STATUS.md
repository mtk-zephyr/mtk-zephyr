# STATUS — as of 2026-09-02 (second update)

Current state of the MediaTek Genio work on `github.com/mtk-zephyr/mtk-zephyr`. This file is
overwritten on every update; `git log` on this branch is the history.

## Branch tips

| Branch | Tip | Contents | Verified |
|---|---|---|---|
| `main` | `5a56224939a` | upstream Zephyr mirror, no MediaTek work | n/a |
| `mtk-genio-dev` | `bfaca73809d` | 19 commits (drop 2026-09-02) | builds + full compliance, **no hardware** |
| `mtk-v4.4.2` | `c4333dd7d9c` | 18 commits on the `v4.4.2` release tag | builds + compliance, **no hardware** |

Tag `verified/2026-09-02` pins the tested `mtk-genio-dev` SHA. Its message predates the license-check
fix below; license/copyright is now verified for that same SHA.

## What is verified on `mtk-genio-dev` (`bfaca73809d`)

| Gate | Result |
|---|---|
| `git am` of all 19 patches onto `origin/main` | clean, no conflicts |
| `mt8390_genio_700_evk/mt8188/a55` | PASS — RAM 144 KB / 2 MB |
| `mt8370_genio_510_evk/mt8188/a55` | PASS — RAM 144 KB / 2 MB |
| `west boards \| grep genio` | both boards listed |
| 5 ADSP targets (mt8186/8188/8195/8196/8365) | PASS on branch and on base |
| ADSP behaviour neutrality | 0 config removals, **loadable binaries byte-identical** to base, all five |
| `check_compliance.py -c origin/main..HEAD` | **exit 0**, no crashes, 1 warning (see findings) |
| `LicenseAndCopyrightCheck` | **PASSED** — see "recently fixed" below |
| `checkpatch.pl -g origin/main..HEAD` | 0 errors, 0 warnings across all 19 |
| `get_maintainer.py` on both `board.yml` | resolves to MediaTek Platforms / aary-p |
| Subject lengths | all within 75 (longest exactly 75) |
| Author / committer | `Aary Patil <aary.patil@mediatek.com>` on all 19 |

## Recently fixed: the license check now works

`LicenseAndCopyrightCheck` previously **crashed** on `reuse` 6.2.0 from PyPI. Installing the exact
revision upstream CI pins (`scripts/requirements-actions.txt:1360`) fixes it, and the check now runs
and passes.

**It must be force-reinstalled.** The pinned revision reports version `6.2.0` — the same string as
the broken release — so a plain `pip install` reports "already satisfied" and does nothing while the
check keeps crashing:

```bash
pip install --force-reinstall --no-deps \
  'reuse @ https://codeberg.org/fsfe/reuse-tool/archive/f94ae0b1bb1d4d2ea91f49df88f3184c5d781a1d.tar.gz'
```

Verify by attribute, not version: `custom_properties` must be a field of
`reuse.global_licensing.AnnotationsItem`.

## What is NOT verified — read before writing any PR description

**Hardware: nothing at all.** The board is not connected. No boot has ever been performed on either
Genio 700 or Genio 510, on either branch. Two changes have failure modes **no build can detect**:

- **`IRQ_TYPE_LEVEL` in the MT8188 dtsi.** Zephyr's GIC flags cell encodes only level-vs-edge, and
  Linux's `IRQ_TYPE_LEVEL_HIGH` shares a value with `IRQ_TYPE_EDGE`. Wrong here means arch-timer PPIs
  are edge-triggered — visible only as inaccurate `k_sleep`, never as a compile error.
- **The GIC MMU restore on `mtk-v4.4.2`.** Silent fault during interrupt setup, before the console
  exists. Needs a boot **on that branch specifically**; a boot on `mtk-genio-dev` proves nothing,
  since that branch does not carry those entries.

**`cntfrq` unconfirmed.** Needs `printk("cntfrq = %u\n", sys_clock_hw_cycles_per_sec());` on a booted
board. If it is not 13 MHz, fold the fix into `GENIO: soc: mediatek: mt8188: add the a55 cpucluster`.

## Open findings

| # | Finding | State |
|---|---|---|
| 1 | `MmuRegionsCheck` warns on the PINCTRL entry in `mt8188/a55/mmu_regions.c`. The checker flags every added `MT_DEVICE` entry unconditionally; a comment does not silence it. | **decided: keep the mapping**, defend upstream. Four other arm64 SoCs do the same; `pinctrl_configure_pins()` runs at `PRE_KERNEL_1` with no `struct device`. |
| 2 | PR A blocked on board docs: **28** `TODO(` markers here (13 + 15) and **0** board `.webp` files. Authoring side's local tree is at 6 and 2; fixes arrive with a future drop. | blocking PR A |
| 3 | `mtk-v4.4.2` diverges from `mtk-genio-dev`: 4 A55 cores not 6, no Genio 510 board, no docs. | **decided: frozen**, single migration pass later. No longer flagged as an issue. |

## Standing rule — GIC entries and `MmuRegionsCheck`

`MmuRegionsCheck` decides the GIC is arch-mapped by testing only whether the file includes
`arch/arm64/arm_mmu.h`. It does not check whether the base actually maps the GIC.

**On any base whose `arch/arm64/core/mmu.c` lacks `mmu_gic_regions`, a "redundant GIC entry" finding
is a false positive and the entries must stay.** Removing them there causes a silent fault with no
console output — this is exactly how the original v4.4.2 bug was introduced.

Test the base, never the branch name:

```bash
git show <ref>:arch/arm64/core/mmu.c | grep -c mmu_gic_regions   # 0 => entries are required
```

Note the check **does not exist** in v4.4.2's own `check_compliance.py`, so it cannot fire on that
branch today. The rule applies at migration time, or if a newer checker is pointed at that tree.

## Doc gates for PR A

Both must hold:

```bash
grep -rc 'TODO(' boards/mediatek/ | grep -v ':0'      # expect no output
ls boards/mediatek/*/doc/img/*.webp | wc -l           # expect 2
```

The second is not redundant: `gen_boards_catalog.py` falls back through `**/*.{ext}`, so if one board
photo is missing the other board's image is displayed silently, which a `TODO(` grep cannot detect.

## Environment notes that affect how results should be read

- **The west workspace tracks one branch's manifest at a time.** Building a branch without a matching
  `west update` produces phantom failures in unrelated modules. Every build result here came from a
  matched workspace.
- **`check_compliance.py` defaults to `HEAD~1..HEAD`** — one commit. All runs here pass
  `-c <base>..HEAD` explicitly.
- **Interactive git flags are unavailable** on the build machine — no `git rebase -i`, including
  `--autosquash`. Fixes are folded into commits by detach → amend → cherry-pick replay.
- Toolchain: `clang-format` 22.1.0, Zephyr SDK 1.0.1 (aarch64 + Xtensa), `reuse` at the CI-pinned
  revision.

## Division of work

The authoring side can now run `check_compliance.py` (minus the license check and eight
workspace-dependent checks) and `checkpatch.pl`. Builds, the workspace-dependent gates and hardware
remain only on the build machine, and are run in full rather than trusting an upstream pass.

If the authoring side gains adb access to the boards, the proposed split is: build machine produces
`zephyr.bin` and owns anything that amends a commit; authoring side drives the boards for
observational tests. **Every hardware report must quote the md5 of the `zephyr.bin` actually loaded**,
not just the source SHA — a SHA identifies what should have been built, not what was, and a stale
image is otherwise indistinguishable from a real regression.

## The two branches differ on purpose

Four files are deliberately **not** identical, because `mtk-v4.4.2` sits on a release tag predating
several upstream changes. A file being identical on both is not reassurance — it may be the bug.

| File | `mtk-genio-dev` | `mtk-v4.4.2` |
|---|---|---|
| `drivers/serial/uart_mtk_common.{c,h}` | `void uart_mtk_irq_update()` | `int`, `return 1` |
| `soc/mediatek/mt8xxx/Kconfig` | `SOC_MTK_ADSP` selects `CPU_HAS_DCACHE`, `ARCH_HAS_NOCACHE_MEMORY_SUPPORT` | neither select exists |
| `soc/mediatek/mt8xxx/Kconfig.defconfig` | `configdefault DCACHE` | `config DCACHE` / `default y` |
| `soc/mediatek/mt8xxx/mt8188/a55/mmu_regions.c` | pin controller only (arch core maps the GIC) | pin controller **plus both GIC banks** |
