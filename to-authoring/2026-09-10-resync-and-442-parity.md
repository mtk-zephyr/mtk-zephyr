# 2026-09-10 — PR A submitted; both work branches resynced, mtk-v4.4.2 brought to parity

## PR A is upstream

PR A is pushed.

Submitted as a single PR, no RFC, from `mtk-genio` on `mtk-zephyr/zephyr`. That branch
is **frozen at `bf26ae3a4c3`** and was deliberately not touched by any of the work
below.

**Review feedback must land in two places.** `mtk-genio-dev` has since been rebased, so
its shas no longer match the commits in the PR. Fixing a review comment on one branch
does not carry to the other. The PR branch is the one reviewers see; `mtk-genio-dev` is
the one everything else is generated from.

## main

Fast-forwarded 1069 commits, `5a56224939a` -> `3860b8cb663`, zero local commits, clean
mirror of zephyrproject-rtos.

## mtk-genio-dev — rebased

`d60c7e1f589` -> `d4dbd67dc1d`, 18 commits rebased onto the new `main`.

**No conflicts** across 1069 commits of drift. All 18 messages, authors, 18
`Signed-off-by` and 12 `Co-authored-by` byte-identical; all 52 added files
byte-identical.

## mtk-v4.4.2 — rebuilt at full parity

`c4333dd7d9c` -> `ee452133d05`, still on the `v4.4.2` tag (still the newest 4.4.x).

The branch had drifted well behind. It was missing the **entire Genio 510 EVK board**,
**both board doc pages**, all 12 co-author trailers, the reviewed commit messages, the
squashed MAINTAINERS commit, the 2026 copyright headers, and it described the MT8188 as
a **4-core** part with `ram: 2048`. The current dev series was replayed onto `v4.4.2` to
fix all of that: 39 files, +544/-41.

### The four deliberate deltas were preserved

These are NOT drift and must never be "fixed" into consistency:

| File | v4.4.2 keeps | why |
|---|---|---|
| `mmu_regions.c` | static `GIC_DIST`/`GIC_REDIST` entries | on `main` these come from `arch/arm64/core/mmu.c` generically; on 4.4.2 they do not. Dropping them is a **silent boot failure** — no console output at all |
| `uart_mtk_common.c` | `int uart_mtk_irq_update` + `return 1` | the 4.4.2 `uart_driver_api` declares it returning `int` |
| `uart_mtk_common.h` | matching `int` declaration | same |
| 700 `_defconfig` | `CONFIG_D/ICACHE_LINE_SIZE_DETECT=y` | 4.4.2-only; the dev branch has never carried these |

Verified after the replay: of the 52 files the series adds, **exactly these four differ
from `mtk-genio-dev`** and nothing else.

### How the conflicts were resolved

Ten "registration point" files (Kconfig, CMakeLists, MAINTAINERS.yml,
`boards/mediatek/index.rst`) also changed upstream between `v4.4.2` and `main`, so a
plain cherry-pick conflicts. Each is touched by exactly one commit in the series, so its
post-commit state is its final state and one resolution per file suffices.

The rule applied was **v4.4.2's content plus our delta, never upstream's drift** — a
three-way merge with `main:<file>` as the base. For eight of the ten this independently
reproduced the previously verified branch byte for byte, which is a strong check that
the rule was right rather than merely plausible. `boards/mediatek/index.rst` legitimately
differs because our delta grew a `toctree` since. `soc/mediatek/mt8xxx/Kconfig` could not
be merged mechanically — `v4.4.2` lacks two upstream `select` lines that appear as
context — so it took the known-good version directly.

Worth knowing: both `MAINTAINERS.yml` and `index.rst` use globs rather than enumerating
boards, so adding the 510 needed no entry in either.

## Verification — both branches, both boards

| | `mtk-genio-dev` | `mtk-v4.4.2` |
|---|---|---|
| gates | pass | pass |
| Genio 700 | 13/13 | 13/13 |
| Genio 510 | 13/13 | 13/13 |
| per-commit sweep | 29/29 bisectable | 29/29 bisectable |

Image size 156 KB on dev, 152 KB on 4.4.2, both into the 8 MB window. `k_sleep(5s)`
worst deviation 4-5 ms on every run; `rx=10065 isr=10065` on every run.

Backup tags if anything needs recovering: `pre-resync-dev` at `d60c7e1f589`,
`pre-resync-442` at `c4333dd7d9c`.

## Note for whoever runs the suite next

Each branch needs its own `west update`; building one branch against the other's modules
produces phantom failures. Pass `--base-ref` to match the branch (`origin/main` for dev,
`v4.4.2` for the customer branch) or compliance and checkpatch span the whole release gap.
