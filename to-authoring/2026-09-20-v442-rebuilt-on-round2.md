# mtk-v4.4.2 rebuilt on PR A review round 2

`mtk-v4.4.2` is at `05ef8eea7ec`, 22 commits on the `v4.4.2` tag: the 16 round-2
PR A commits plus the 6 PR B commits — the same series `mtk-genio-dev` carries,
adapted to this base.

Rebuilt cleanly rather than grown append-only, because no customer is on the
branch yet. Once one is, the append-only contract in `STATUS.md` applies and this
kind of rewrite stops being available.

## The four deliberate deltas, and a new one

This branch is not a stale copy of the dev branch; it differs on purpose, because
its base differs. Each of these is a silent failure if dropped.

| file | v4.4.2 keeps | why |
|---|---|---|
| `a55/mmu_regions.c` | static `GIC_DIST` / `GIC_REDIST` entries | **new this round** |
| `uart_mtk_common.h` | `int uart_mtk_irq_update(...)` | `uart_internal.h:125` declares the callback returning `int` here, `void` on main |
| `uart_mtk_common.c` | same, plus `return 1;` | as above |
| 700 `_defconfig` | `CONFIG_D/ICACHE_LINE_SIZE_DETECT` | v4.4.2-only; the dev branch has never carried them |

### Why mmu_regions.c had to come back

PR A review round 1 **deleted** that file upstream, to satisfy `MmuRegionsCheck`.
Two facts make that wrong for this base:

- `MmuRegionsCheck` does not exist at v4.4.2 — nothing there would have complained.
- `arch/arm64/core/mmu.c` at v4.4.2 has **no `mmu_gic_regions[]`**; main grew that
  later. So on this base nothing maps the GIC unless the SoC does it.

Deleting it here is a board that never reaches the console.

It was re-created with the **GIC entries only**. The PINCTRL entry the old branch
also carried is deliberately gone: from round 1 the pin controller maps its own
window with `device_map()`, and a second mapping of the same physical range is
refused by the arm64 MMU — `entry already in use`, which is the panic PR B hit in
September. Too few mappings and the GIC faults before the console exists; too many
and the MMU refuses. Both are silent. H1 booting to a live console is what proves
the file is right.

## Conflicts, and how they were resolved

Two files would not three-way merge, both because round 2 replaces their entire
body: `soc/mediatek/mt8xxx/Kconfig` becomes an `if`/`rsource` wrapper, and
`Kconfig.defconfig` shrinks from 114 lines to a 4-line `orsource`. Nothing from
either base survives, so taking the round-2 version is correct.

They are listed by name in the replay script with the reason, rather than handled
by a "take theirs when the merge fails" fallback. That fallback is exactly how
this branch's own adaptations would get silently swallowed.

## Verification at `05ef8eea7ec`

| | |
|---|---|
| gates | 9/9 |
| checkpatch | **0 errors**, 2 warnings (see below) |
| compliance | warnings only |
| ADSP neutrality, five targets | byte-identical loadables |
| per-commit sweep | 39/39, bisectable |
| Genio 700 | **11/11** — full tier plus both GPIO tiers |

The ADSP result carries real weight here. Round 2 moves the per-SoC cache defaults
out of `Kconfig.defconfig` into per-SoC `select`s, while v4.4.2 reached the same
symbols by a different route (`config ... default y`, not `configdefault`).
Byte-identical images on all five targets is the evidence those routes are
equivalent on this base.

Only the 700 was tested, by agreement. The 510 differs from it only in board dts.

## Two suite bugs this run exposed

Both were mine, and both were fixed rather than worked around.

**G4 reported `lost: CPU_HAS_DCACHE` on all five DSP targets. It was not lost** —
the symbol is `=y` in both configs and had only moved position in the generated
`.config`. The classifier did a line diff, which is order-sensitive, so a reordered
symbol showed as removed *and* added. It now compares symbol sets. Verified by
replaying against the same builds (all pass) and by a negative control: strip
`CONFIG_XTENSA` from a config and it is still correctly reported lost.

**G3 failed on 2 checkpatch warnings.** `gpio_mt8188.c` is byte-identical on both
branches, but v4.4.2 ships an older `checkpatch.pl` whose `LINE_SPACING` heuristic
misreads `DEVICE_MMIO_NAMED_ROM(reg_base);` inside a struct as a statement
following a declaration. Zero errors across all 22 commits.

The code was **not** changed to satisfy it. A blank line inside a struct, to
appease a false positive from an older checker, on a branch that is never
upstreamed, would have created a fifth delta for nothing. G3 now separates
severity: errors fail, warnings pass with the count always printed, so a genuine
warning regression stays visible.

## Outstanding

- `mtk-v4.4.2` is now level with `mtk-genio-dev`. When PR A moves again, this
  branch takes the change only once it is structurally settled — not on a
  schedule. Round 1 would have been a wasted port; it existed for days.
- The dropped `*_mtk_common.c` split and the `Co-authored-by` question on the
  rewritten drivers are both still open, and both matter more now that PR C is
  being authored.
- Four rpmsg inmate cells still grant 2 MB.
