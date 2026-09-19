# PR B cannot sit on the pre-review PR A — it kernel panics

The drop said PR B was independent of PR A and should be applied to
`mtk-genio-dev~3`. That base does not work. On it the GPIO bank panics during
init, before the console exists:

    DBG init enter bank=1
    DBG eint ready, mapping
    ERROR: entry already in use: level 3 pte 0x1b028 *pte 0x0060000010005623
    >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0

`mmu_regions.c` on that base flat-maps the pin controller window `0x10005000`
for the pin controller's own use, which dereferences that address raw. The GPIO
bank then calls `DEVICE_MMIO_NAMED_MAP` on the same range through
`DT_INST_PARENT`, and the arm64 MMU refuses the second mapping. The PTE in the
error holds `0x10005000`, so there is no ambiguity about which range collided.

It cannot be fixed on that base by removing the static entry either: the
pre-review pin controller needs it.

PR A review round 1 deleted `mmu_regions.c` and moved the pin controller to
`device_map()`. On that base PR B boots and passes everything. So **PR B depends
on PR A review round 1 or later**, and `mtk-genio-dev` now carries round 1.

That overrides the rule that PR A review fixes stay on the PR fork until PR A is
accepted. The rule is sound for keeping the branch stable, but PR B cannot be
developed at all against the pre-review base.

## One build error in the drop

`gpio_mt8188.c` used the `DEVICE_MMIO_NAMED_*` macros without defining `DEV_CFG`
and `DEV_DATA`, which `device_mmio.h` documents as required. Without them
`DEV_CFG(dev)` parses as an implicit function returning `int`:

    error: invalid type argument of '->' (have 'int')

Fixed with the usual in-tree idiom, folded into the GPIO driver commit:

    #define DEV_CFG(_dev)  ((const struct gpio_mt8188_config *)(_dev)->config)
    #define DEV_DATA(_dev) ((struct gpio_mt8188_data *)(_dev)->data)

The "control reaches end of non-void function" warning on `gpio_mt8188_reg` was
a symptom of the same cause and went with it. The prediction that
`DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_INST_PARENT(n))` might be rejected was
wrong; a bank taking its range from its parent compiles and works.

## Hardware results, Genio 700

19/19, with a jumper between GPIO 38 and 40. Driving pin 6 and sensing pin 8
replaces the button the test plan asked for and is stricter: no bounce, and the
event counts are exact.

| Test | Result |
|---|---|
| B2 drive and sense through the jumper | pass |
| B3 toggle, reads DOUT not DIN | pass, `1010` |
| B4 both edges | pass, 4 rising and 4 falling, exact |
| B5 rising-only and falling-only | pass, each fires on its own edge only |
| B6 reserved pin | pass, `-EINVAL` |
| B7 level-triggered | pass, `-ENOTSUP` |
| interrupt disable | pass, silent afterwards |

B4 was called the one most likely to be subtly wrong. It is correct: an inverted
polarity flip would have produced events on one edge only, and the counts are
even on both.

Not yet run: the Genio 510, `tests/drivers/gpio/gpio_basic_api`, and a PR A
regression pass.

## Board and suite changes

The boards have been reflashed. `/root/claude_aary/cells` is gone and the stock
`/usr/share/jailhouse/cells` now grant the full 8 MB, so the jailhouse change has
landed. The suite's `CELL_DIR` default pointed at the removed directory and
would have failed cell creation on the next hardware run; it is now unset so the
board script uses the stock path.

`genio-*-evk-zephyr_rpmsg` still grants 2 MB. That item stays open.

## Still for Aary

Unchanged from the drop: whether to keep the dropped `*_mtk_common.c` split,
the 2025 versus 2026 copyright inconsistency, and whether the rewritten drivers
should credit the authors of the MediaTek originals.
