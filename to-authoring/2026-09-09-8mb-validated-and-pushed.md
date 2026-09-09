# 8 MB window validated on both boards; the drop is pushed (2026-09-09)

Your 2026-09-04 drop is on `origin/mtk-genio-dev` at **`361cbfaca1f`**, 19 commits.
The pushed tree hash is **`e4cbf7a6315e`** — exactly the one you specified, so what
is on GitHub is bit-for-bit what you authored, `.webp` binaries included.

## The 8 MB window is now real, not just declared

§2c of your handover was right to flag this as the risky change, and right that a
plain boot cannot detect it. It could not be validated when the drop arrived,
because **the installed cells granted only 2 MB**. New cells have since been built
and installed, and the window is now genuinely 8 MB on both boards.

The check that proves it places a 6 MB array in `.bss`, so the linker extends the
image across the window and boot-time zeroing touches every byte:

```
MEMTEST window base=0x8000 size=0x800000 (8 MB)
MEMTEST probe array 6 MB at 0x27a80..0x627a7c
MEMTEST DONE mismatches=0 -> PASS
```

`0x627a7c` is three times past the old 2 MB ceiling at `0x208000` — the exact
address range that previously dropped the cell to `failed`. This is now **H8** in
the suite.

One correction to your §2c for the record: your checkout showing the plain Zephyr
cell at `0x00200000` was **accurate, not stale**, and the AFE variant was at 4 MB
rather than the 8 MB you were told. The root cells did already reserve the full
`0x6b000000`–`0x6b800000`; only the inmate grants were short.

## Suite results — 13/13 on both boards

| | Genio 700 | Genio 510 |
|---|---|---|
| G1 both Arm builds | PASS (156 KB / 8 MB) | PASS |
| G2 compliance | exit 0, 1 warning (PINCTRL, accepted) | same |
| G3 checkpatch | 0 errors, 0 warnings | same |
| H1 boot + board string | PASS | PASS |
| H2 `cntfrq` / `k_sleep` | 13 MHz / worst 5 ms | 13 MHz / worst 13 ms |
| H3 RX + interrupt load | `rx=10065 isr=10065` | `rx=10065 isr=10065` |
| H4 runtime reconfigure | PASS incl. negative control | PASS |
| H5 `uart_basic_api` | 7/7 | 7/7 |
| H6 `uart_interrupt_api` | 1/1 | 1/1 |
| H7 cell cycling | 6/6 | 6/6 |
| H8 memory window | 8 MB PASS | 8 MB PASS |

Interrupt counters identical to the byte; timing within 8 ms of each other. No
board-specific behaviour anywhere in the driver stack.

## Outstanding — four cell configs were missed

Of the seven inmate cell configs that needed the window widened, **three landed
and four did not**:

| Cell | Window |
|---|---|
| `genio-700-evk-zephyr.cell` | 8 MB — correct |
| `genio-510-evk-zephyr.cell` | 8 MB — correct |
| `genio-700-evk-zephyr-afe.cell` | 8 MB — correct (was 4 MB) |
| `genio-700-evk-zephyr_rpmsg_native.cell` | **2 MB** |
| `genio-700-evk-zephyr_rpmsg_openamp.cell` | **2 MB** |
| `genio-510-evk-zephyr_rpmsg_native.cell` | **2 MB** |
| `genio-510-evk-zephyr_rpmsg_openamp.cell` | **2 MB** |

All four root cells correctly reserve 8 MB, so this is purely the inmate grants.
**Zephyr's window size is per-board; the grant is per-cell-configuration**, so
every cell that loads a Zephyr image built from these boards must grant at least
what the devicetree declares. Running Zephyr under an rpmsg cell today would
reacquire the exact latent fault: boots `hello_world` fine, faults under memory
pressure. Aary is aware; it is a MediaTek-side rebuild.

## The test suite

`scripts/` has been replaced by `tests/`, a runnable suite:

```bash
./tests/run-tests.sh            # everything available
./tests/run-tests.sh --gates    # no board needed
./tests/run-tests.sh --list     # what would run
```

Everything is built from the current checkout, the board is auto-detected over
adb, and every transfer is md5-verified. Exit status is 0 only if nothing failed.
`tests/README.md` documents each test with expected output and — importantly —
what it deliberately does **not** prove.

`tests/tools/verify-jailhouse-cells.py` parses cell binaries and checks the inmate
window against what the devicetree declares. **It is what caught the four missed
rpmsg cells, before anything reached a board.** Worth running on any future cell
drop.

## Your finalisation plan (§5)

Step 1 is done: the content is final and pushed. Steps 2 onward — sync onto fresh
upstream `main`, re-verify, message amendments and co-authors, then the submission
transform — are Aary's to sequence. Two notes that still apply:

- `origin/main` is still `5a56224939a` from 1 September. The last sync brought
  +3259 commits and one behavioural change that mattered.
- **Do not tag before building.** `verified/*` as an *annotated* tag shadows the
  SHA in the boot banner, because Zephyr's version string comes from
  `git describe`. Use a lightweight tag, or tag after the images are built.
