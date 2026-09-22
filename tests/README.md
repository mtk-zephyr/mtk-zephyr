# MediaTek Genio — Zephyr test suite

Regression tests for the Genio Zephyr port on `mtk-genio-dev` / `mtk-v4.4.2`.
Everything is built from the current checkout, so a run exercises your changes
rather than whatever happened to be flashed last.

Mirrored on the `handover` branch of `github.com/mtk-zephyr/mtk-zephyr` under
`tests/`, so the authoring agent — which cannot build or flash — can read the
sources and expected output.

```bash
./run-tests.sh              # everything available
./run-tests.sh --gates      # no board needed
./run-tests.sh --hardware   # board only
./run-tests.sh --adsp       # add the slow ADSP neutrality check
./run-tests.sh --gpio       # add H9; needs a jumper between GPIO 38 and 40
./run-tests.sh --list       # what would run
```

Set `ZT_AGENT` (`dev-agent` / `pr-agent`) so a run is attributable: the suite is
shared between two agents and results are written per run, not per file.

Exit status is 0 only if nothing failed, so it can gate a commit.

---

## Layout

```
run-tests.sh              the runner
lib/common.sh             board detection, port handling, build/flash helpers
firmware/hwtest/          cntfrq + three 5 s sleeps
firmware/rxtest/          interrupt-driven echo with rx/isr counters
firmware/reconf/          runtime baud change 115200 -> 9600 -> 115200
firmware/memtest/         6 MB .bss array proving the granted window
firmware/gpiotest/        GPIO and EINT drivers, needs a jumper 38 <-> 40
host/uartlog.py           timestamping serial logger
host/rxtest_host.py       drives the echo test, verifies byte-for-byte
host/reconf_host.py       samples the console at each baud
host/uartapi_host.py      drives the keyboard-harness ztest suites
board/setup-g700.sh       board-side cell bring-up
board/setup-g510.sh       likewise
board/setup-afe.sh        bring-up under the cell that grants the Audio Front End
board/cells/              cell configs that are not in the stock rootfs
tools/verify-jailhouse-cells.py   inspect cell binaries (not wired into the run)
tools/bisect-build.sh     build every commit in a range (not wired into the run)
tools/run-firmware.sh     flash and run one image by hand, with a safe logger
logs/<run>/               one directory per run; logs/latest points at the newest
```

### Before submitting a series: `tools/bisect-build.sh`

The gates build the tip. Upstream requires every *commit* to build
(`doc/contribute/contributor_expectations.rst`), and a tip-only run cannot see a
series that is broken in the middle:

```bash
./tools/bisect-build.sh upstream-zephyr/main my-submission-branch
```

This is not hypothetical. Splitting a validated 18-commit branch into two PRs
produced a first PR that referenced `cpu@400`/`cpu@500` while the commit adding
those nodes stayed behind in the second, so the first failed `dtc` on its own.
Every build until then had used the whole branch, so nothing caught it.

## Requirements

| | |
|---|---|
| Zephyr tree | `$ZEPHYR_BASE`, default `~/zephyrproject/zephyr` |
| Python venv | `$VENV`, default `~/zephyrproject/.venv` — needs `pyserial` |
| Board | Genio 700 or 510 EVK over `adb`, auto-detected from `uname -n` |
| Serial | `$PORT`, default `/dev/ttyUSB0`, 115200, console on **CN3201** |
| Board dir | `$BOARD_DIR`, default `/root/claude_aary`, holding `setup-g*.sh` |
| Cell dir | `$CELL_DIR`, unset by default so the board script uses `/usr/share/jailhouse/cells`. Those now grant the full 8 MB the board devicetree declares. It previously defaulted to a patched copy under `$BOARD_DIR`, which has since been removed from the boards — pointing at it now fails cell creation outright. Set it only to test a specific set of cell configs. Note `genio-*-evk-zephyr_rpmsg` still grants 2 MB |
| Compliance base | `$BASE_REF` or `--base-ref`, default `origin/main`. **A branch based on a different upstream must set this** — e.g. `--base-ref upstream-zephyr/main` for the submission branch — or compliance and checkpatch span hundreds of unrelated commits |

Override any of them as environment variables.

**Run `west update` first if you have switched branches.** The workspace tracks
one branch's manifest at a time; building with a mismatched `modules/` produces
phantom failures in unrelated code — an `ESP32P4` Kconfig error from
`modules/hal/espressif` is the one that has bitten us.

---

## Gates — no board required

### G1 — build both Arm board targets

Builds `samples/hello_world` for `mt8390_genio_700_evk/mt8188/a55` and
`mt8370_genio_510_evk/mt8188/a55`.

Expected: both succeed. The reported region is worth a glance — it should read
`RAM: <n> KB   8 MB`. **If the denominator says 2 MB, the devicetree memory
node did not take effect.**

### G2 — compliance

```
./scripts/ci/check_compliance.py -c origin/main..HEAD
```

Expected: exit 0, `1 check(s) with warnings only` — the `MmuRegionsCheck`
warning on the PINCTRL entry, which is **accepted and deliberate**. The checker
flags every added `MT_DEVICE` region unconditionally; a comment does not silence
it, and the mapping is load-bearing because `pinctrl_configure_pins()` runs at
`PRE_KERNEL_1` with no `struct device`.

Two traps this has hit before:

- The script defaults to `HEAD~1..HEAD` — **one commit**. Always pass
  `-c <base>..HEAD` or you are checking almost nothing.
- `LicenseAndCopyrightCheck` *crashes* on the PyPI `reuse` 6.2.0 instead of
  reporting. Install the revision upstream CI pins, and note the fixed revision
  reports the *same* version string, so a plain `pip install` says "already
  satisfied" and does nothing:

  ```bash
  pip install --force-reinstall --no-deps \
    'reuse @ https://codeberg.org/fsfe/reuse-tool/archive/f94ae0b1bb1d4d2ea91f49df88f3184c5d781a1d.tar.gz'
  ```

  Verify by attribute, not version: `custom_properties` must be a field of
  `reuse.global_licensing.AnnotationsItem`.

### G3 — checkpatch

```
./scripts/checkpatch.pl -g origin/main..HEAD
```

Expected: every commit `0 errors, 0 warnings`.

### G4 — ADSP behaviour neutrality  *(`--adsp`, slow)*

The SoC reorganisation must not change the five audio DSP platforms. Builds
`mt8186`, `mt8188`, `mt8195`, `mt8196`, `mt8365` ADSP targets from HEAD **and**
from `origin/main`, then compares.

Expected per target: **zero removed config symbols** and a **byte-identical
loadable image** (`objcopy -O binary`, md5).

Symbols are *added* by the cpucluster split — currently
`CONFIG_SOC_FAMILY="mt8xxx"`, `CONFIG_SOC_FAMILY_MT8XXX=y`, `CONFIG_MTK_ADSP=y`
and the per-SoC `CONFIG_SOC_MT81xx_ADSP=y` — and cannot help but appear against
a base that predates them. **The removals are the half with teeth**: a removal
means a DSP lost `XTENSA` or its `Kconfig.defconfig` body, which no compile
error would catch.

A removal is tolerated only when it is a *declared* rename whose replacement
appears in the same diff, listed in `$ADSP_RENAMES` as `OLD=NEW`. PR A review
round 1 renamed the family symbol, so the default is
`SOC_FAMILY_MTK=SOC_FAMILY_MT8XXX`. Writing the pair down is the point: an
accidental drop cannot hide behind a threshold that was simply relaxed.

Costs ten builds; that is why it is opt-in.

---

## Hardware tests

Auto-detects the board and picks the matching target and setup script.

### H1 — boot and board string

Builds `firmware/hwtest`, transfers it (verifying by md5), starts the cell.

Expected:

```
*** Booting Zephyr OS build v4.4.0-13857-g<sha> ***
Hello World! mt8390_genio_700_evk/mt8188/a55
```

The board string must match the board you are holding. This is not a formality:
a Genio 700 image runs fine on 510 hardware because the parts are pin-compatible,
so the banner is the only thing that catches a mismatched image or cell.

### H2 — `cntfrq` and timer accuracy

Same image. Prints `sys_clock_hw_cycles_per_sec()`, then three
`k_sleep(K_SECONDS(5))` intervals bracketed by markers.

Expected: `cntfrq = 13000000` (13 MHz, confirmed on both parts), and each
interval within 100 ms of 5 s — observed 5.004–5.012 s, i.e. under 0.25 %.

**Why the host does the timing.** `k_uptime_get()` derives from the very timer
under test, so it cannot detect a misconfigured one. `uartlog.py` stamps each
line on arrival, giving an independent clock. This is the only check that
catches the `IRQ_TYPE_LEVEL` class of bug: Zephyr's GIC flags cell encodes only
level-versus-edge, and Linux's `IRQ_TYPE_LEVEL_HIGH` shares a value with
`IRQ_TYPE_EDGE`, so getting it wrong leaves the arch-timer PPIs edge-triggered —
invisible to any build.

One artefact to know: on a first boot, the banner, `cntfrq` and `SLEEP_START 0`
can all arrive in one burst once the console comes up, which makes iteration 0
read about a second short. Iterations 1 and 2 are the reliable ones. The runner
takes the worst deviation, so a burst can cause a spurious failure — re-run
before believing it.

### H3 — UART RX and interrupt load

Builds `firmware/rxtest`: an interrupt-driven echo that counts received bytes
and ISR entries. The host sends known data and compares the echo byte-for-byte,
then asks for counters with an out-of-band `0x04` that is counted but not
echoed, so the counter line cannot interleave into the stream being verified.

Three phases: 64 bytes, 2000 bytes in 64-byte chunks, 8000 bytes in 256-byte
chunks.

Expected: all three byte-for-byte identical, and

```
STATS rx=10065 isr=10065
```

matching the host's total exactly — **zero bytes lost across 10,065 interrupts**.

`isr == rx` means one interrupt per byte: the RX FIFO trigger is effectively 1
and nothing batches. Not a defect, but it makes RX interrupt-bound — about
6.5 KiB/s of an 11.5 KiB/s line rate, with the echo done inside the ISR via
`uart_poll_out`. Raising the FIFO trigger level is where to look if RX
throughput ever matters.

### H4 — runtime UART reconfigure

Builds `firmware/reconf`, which reads the config back, moves the console to
9600, then restores 115200, announcing each phase repeatedly so the host can
resynchronise.

Expected, all four:

| Phase | at 115200 | at 9600 |
|---|---|---|
| 1 before the change | readable | — |
| 2 after switching | **garbage** | readable |
| 3 after restoring | readable | — |

**The negative control is the actual evidence.** Phase 2 being *unreadable* at
115200 is what distinguishes a real divisor change from a `uart_configure()`
that returned 0 and did nothing.

### H5 — `tests/drivers/uart/uart_basic_api`

The in-tree suite. It is declared `harness: keyboard`, so **twister cannot run
it** — it waits for a human to type. `uartapi_host.py` supplies the keystrokes
over the same serial line.

Expected: 7/7 and `PROJECT EXECUTION SUCCESSFUL`.

```
SUITE PASS - 100.00% [uart_basic_api]: pass = 6, fail = 0, skip = 0
 - test_uart_config_get     - test_uart_configure
 - test_uart_fifo_fill      - test_uart_fifo_read
 - test_uart_poll_in        - test_uart_poll_out
SUITE PASS - 100.00% [uart_basic_api_pending]: pass = 1, fail = 0, skip = 0
 - test_uart_pending
```

### H6 — `tests/drivers/uart/uart_interrupt_api`

Expected: `test_uart_fifo_tx_sizes` passes, 1/1.

Upstream scopes this suite `vendor_allow: adi`, so it is **not** a test our
board is expected to run — passing is a bonus, not a gate.

### H7 — cell shutdown/restart cycling

Six shutdown → destroy → create → load → start cycles, counting banners.

Expected: 6/6 clean boots with the right board string each time.

### H8 — the declared memory window is actually granted

Builds `firmware/memtest`, which places a 6 MB array in `.bss` so the linker
extends the image across the window; boot-time `.bss` zeroing then touches every
byte of it.

Expected:

```
MEMTEST window base=0x8000 size=0x800000 (8 MB)
MEMTEST probe array 6 MB at 0x27a80..0x627a7c
MEMTEST DONE mismatches=0 -> PASS
```

**This is the only test that can distinguish an 8 MB Jailhouse grant from a
2 MB one.** A plain boot cannot: `hello_world` never touches high addresses, so
an over-declared window boots cleanly and faults later under memory pressure.
If the grant is short, the cell drops to `failed` during `.bss` zeroing, before
the console exists — so the symptom is *no output at all*, not an error message.

A wrong way to write this test: simply writing to a high address in the declared
window faults regardless of the grant, because `arch/arm64/core/mmu.c` maps
`_image_ram_start`..`_image_ram_end` — the *image extent* — not the whole SRAM
region. Growing `.bss` is what actually extends the mapping.

Check the cells themselves with `tools/verify-jailhouse-cells.py`, which parses
the binaries and compares the inmate window against what you expect:

```bash
python3 tools/verify-jailhouse-cells.py ~/claude/jailhouse-cells/*.cell
```

It only enforces the size on cells named `zephyr` — `uart-demo` is a different
inmate with a legitimately small window — and checks that each root cell reserves
at least what an inmate is expected to claim.

### Suites deliberately not run

The rest of `tests/drivers/uart/` — `uart_async_api`, `uart_async_dual`,
`uart_async_rx`, `uart_async_slip`, `uart_elementary`, `uart_emul`,
`uart_errors`, `uart_mix_fifo_poll`, `uart_pm` — are **not applicable**, not
untested. The driver implements the interrupt-driven API, not the async one, and
the board exposes a single console UART with no loopback or second port.

---

## Two traps worth internalising

**1. An absent console is ambiguous.** "No output" is both a real failure
signature *and* what a dead host-side logger looks like. They are
indistinguishable from the log alone. Before treating silence as a result:

```bash
fuser /dev/ttyUSB0        # is anything actually capturing?
adb shell 'jailhouse cell list'   # does the cell say running or failed?
```

This nearly produced a false report that the v4.4.2 GIC fix had failed, when in
fact the logger had been killed. `start_logger` in `lib/common.sh` now verifies
it holds the port before returning.

**2. Only one process may hold the serial port.** `picocom` is built with
`USE_FLOCK`. The line logger and the interactive host drivers cannot both own
it, which is why the runner starts and stops the logger around each phase. If a
test reports the port busy, something was left attached — `fuser -k /dev/ttyUSB0`.

Also: the two boards use **different FTDI adapters** — `AB0PKARP` on the 700,
`B001I8ZJ` on the 510 — but both enumerate as `/dev/ttyUSB0`. Use
`/dev/serial/by-id/` if both are ever attached at once.

---

### H9 — GPIO and EINT drivers  *(`--gpio`, needs a jumper)*

Builds `firmware/gpiotest`. **Requires a jumper wire between GPIO 38 and GPIO
40** — bank 1 pins 6 and 8, the only two the board devicetree leaves unreserved.
Pin 6 drives, pin 8 senses and takes the interrupt.

Off by default: without the jumper every sense reads back wrong and the failures
say nothing about the code.

```
GPIOTEST device ready, drive=pin6 sense=pin8
PASS B4-both-rising 4 rising edges produced 4 event(s)
GPIOTEST DONE passed=19 failed=0 -> PASS
```

Nineteen checks: reserved pins refused (`-EINVAL`), output drives and input reads
it back, `gpio_pin_toggle` moves the pad, level-triggered interrupts refused
(`-ENOTSUP`), rising-only and falling-only each fire on their own edge and ignore
the other, both-edges, and interrupt disable actually silencing delivery.

**Why software edges rather than a button.** The controller detects one condition
per line, so both-edges is emulated by flipping the polarity inside the handler.
An inverted flip still produces events — on one edge only — which reads as
working code until the events are *counted*. Driving pin 6 in software gives
exact counts with no bounce; a button gives neither. The test asserts four rising
and four falling, separately.

Both parts pass 19/19: MT8390 on the Genio 700 and MT8370 on the Genio 510.

## Running under the Audio Front End cell

The AFE samples are not part of a tier: they need the cell that grants the AFE,
and the loopbacks need wiring. Run one by hand:

```bash
SETUP=setup-afe.sh tools/run-firmware.sh <image.bin> 'PASS --' afe-c8
```

`board/setup-afe.sh` brings the inmate up under
`genio-700-evk-zephyr-afe.cell`, preferring the copy staged in `board/cells/`
over `/usr/share/jailhouse/cells`, because that cell is not part of the stock
rootfs and a reflash takes it. The 700-authored cell is used on the 510 as well:
same die family, AFE at the same addresses, identical inmate window and console.
Only the root cell comes from the board.

Three things about this that are not guessable:

- **The `audio` power domain is off on a stock boot**, and neither the cell nor
  the driver can raise it. `setup-afe.sh` writes `on` to the AFE device's
  `power/control` first. Without it every register write is dropped while every
  driver call returns 0.
- **Tear the cell down before touching Linux's `mt8188-audio` driver.** While a
  cell holds the AFE its registers are unmapped from the root cell, so a bind or
  a runtime resume faults the root cell and resets the board.
- **A run that stops all its streams wedges the next one.** Reboot between such
  runs until that is fixed; the loopbacks are unaffected because they never stop.

## Interpreting a run

```
  PASS  H3 UART RX + interrupt load          rx=10065 isr=10065
  FAIL  H2a cntfrq                           got 'nothing', expected 13000000
```

Each run writes to its own directory, `logs/<timestamp>-<agent>-<branch>/`, with
`logs/latest` pointing at the newest — so a path like `logs/latest/uart.log` still
works. `uart.log` there holds the timestamped console transcript, which is the
only record of what a board actually printed.

Per-run directories exist because the suite is shared: with one fixed log
directory, whichever agent ran second silently destroyed the other's evidence.
That has already prevented one question being settled after the fact.

A failure in H1 with `cell=failed` usually means the image does not fit the
inmate window — check the declared size against the cell. A failure with
`cell=running` but no output means the console never came up: pinctrl, the
infra-ao clock gate, or the UART driver.
