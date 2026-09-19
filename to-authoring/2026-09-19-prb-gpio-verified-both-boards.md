# PR B GPIO and EINT drivers verified on both boards

Both parts pass **19/19**: MT8390 on the Genio 700 (2026-09-18) and MT8370 on
the Genio 510 (2026-09-19), on `mtk-genio-dev` at `344dc286ede`.

## Test rig

A jumper wire between **GPIO 38 and GPIO 40** — bank 1 pins 6 and 8, the only two
the board devicetree leaves unreserved. Pin 6 drives, pin 8 senses and takes the
interrupt.

The test plan asked for a button on GPIO 40 for the both-edges case. The jumper
replaces it and is strictly better: edges are produced in software, so there is no
bounce and the event count is exact. An exact count is the only thing that
separates correct both-edges emulation from an inverted polarity flip, which still
fires — on one edge only — and reads as working code until someone counts.

Firmware is `tests/firmware/gpiotest`; the suite runs it as **H9** behind
`--gpio`, off by default since the jumper is not always fitted.

## What each check proves

| Check | Proves |
|---|---|
| `B6-reserved-pin` | `gpio-reserved-ranges` reached the port mask — `configure(pin 0)` returns `-EINVAL` |
| `B2-configure-output` / `-input` | the bank maps its parent's register range and picks the right word |
| `B2-drive-high` / `-low` | the pad actually moves, read back through the jumper |
| `B3-toggle` | `port_toggle_bits` reads DOUT, not DIN. Sensed `1010` over four toggles. The vendor driver read DIN, where an output held externally never moves |
| `B7-level-refused` | level-triggered interrupts refused with `-ENOTSUP`, deliberately: the block's own output to the GIC is level-triggered too, so a held level re-enters the handler |
| `B5-rising-on-rise` / `-ignores-fall` | `SENS`/`POL` select one condition, and only that condition fires |
| `B5-falling-on-fall` / `-ignores-rise` | the same in the other direction, and the acknowledge-after-change works: without it a line enabled after `set_trigger` fires once for a condition that predates it |
| `B4-both-rising` / `-falling` | **4 rising and 4 falling events for 4 of each** — the dual-edge emulation, counted rather than observed |
| `B4-disable` / `-disabled-silent` | `GPIO_INT_DISABLE` actually stops delivery: zero events afterwards |

Identical results on both parts, so the bank indexing and the reserved-range mask
are right for a 4-core and a 6-core die alike.

## One build error, fixed

`gpio_mt8188.c` used the `DEVICE_MMIO_NAMED_*` macros without defining `DEV_CFG`
and `DEV_DATA`, which `include/zephyr/sys/device_mmio.h` documents as required.
Without them `DEV_CFG(dev)` parses as an implicit function returning `int`:

    error: invalid type argument of '->' (have 'int')

Fixed with the usual in-tree idiom and folded into the GPIO driver commit. The
"control reaches end of non-void function" warning on `gpio_mt8188_reg` was a
symptom of the same cause and went with it.

The drop predicted the risky line would be
`DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_INST_PARENT(n))`. That part is fine — a
bank taking its register range from its parent compiles and works.

## Suite changes that affect how results are read

Runs now write to `logs/<timestamp>-<agent>-<branch>/`, with `logs/latest`
pointing at the newest. The suite is shared between two agents on one workspace,
and a single fixed log directory meant whichever ran second destroyed the other's
console transcripts.

`CELL_DIR` is unset by default. The stock jailhouse cells now grant the full 8 MB;
the patched copies the old default pointed at were removed when the Genio 700 was
reflashed, so that default failed cell creation outright.

`genio-*-evk-zephyr_rpmsg` still grants 2 MB. Unchanged, still open.

## Not yet run

`tests/drivers/gpio/gpio_basic_api` from upstream, and a full hardware regression
pass on either board against the new base. Neither is blocked on anything.

## Still for Aary, unchanged from the drop

Whether to keep the dropped `*_mtk_common.c` split, the 2025 versus 2026 copyright
inconsistency, and whether the rewritten drivers should credit the authors of the
MediaTek originals.
