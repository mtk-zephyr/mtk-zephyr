# UART RX, interrupt load, reconfigure, in-tree suites (2026-09-03)

Everything on the remaining hardware list has now been run on the Genio 700, against
`mtk-genio-dev` `0d9156ec50b`. **All pass.** One pre-existing defect found in both board defconfigs —
details at the end; it is the only thing here needing a change.

## Results

| Test | Result |
|---|---|
| UART RX — echo, 64 bytes | **PASS**, byte-for-byte identical |
| UART RX — 2000 bytes, 64-byte chunks | **PASS**, byte-for-byte identical |
| Interrupt load — 8000 bytes sustained | **PASS**, byte-for-byte identical, 6.5 KiB/s |
| Firmware counters after the above | **`rx=10065 isr=10065`**, host sent exactly 10065 |
| Runtime reconfigure (`UART_USE_RUNTIME_CONFIGURE`) | **PASS**, verified on the wire |
| `tests/drivers/uart/uart_basic_api` | **PASS 7/7**, `PROJECT EXECUTION SUCCESSFUL` |
| `tests/drivers/uart/uart_interrupt_api` | **PASS 1/1** |
| Cell shutdown/restart cycling | **PASS**, 6/6 clean boots |

### RX and interrupt delivery

A firmware echo server with interrupt-driven RX, counting bytes and ISR entries. The host sends
known data and compares the echo byte-for-byte, then requests the counters with an out-of-band `0x04`
that is counted but not echoed, so the counter line cannot interleave into the stream being verified.

**10,065 interrupts delivered, zero bytes lost.** The interrupt-delivery row on your matrix asked for
"a few thousand"; this is ten thousand with an exact byte-level match.

One observation worth recording: **`isr == rx` exactly** — one interrupt per received byte. The RX
FIFO trigger is effectively at 1, so nothing is batching. Not a bug and not blocking, but it means
throughput is interrupt-bound: the sustained figure was 6.5 KiB/s against a 11.5 KiB/s line rate,
with the echo happening inside the ISR via `uart_poll_out`. If RX throughput ever matters, raising
the FIFO trigger level is where to look.

### Runtime reconfigure — proven on the wire, not just via the API

`uart_config_get` returns the expected values, then the firmware moves the console to 9600 and back
to 115200, announcing each phase repeatedly. The host samples at each rate:

| Phase | Listening at 115200 | Listening at 9600 |
|---|---|---|
| 1 — before the change | readable | — |
| 2 — after switching to 9600 | **garbage** (all-zero framing errors) | **readable** |
| 3 — after restoring 115200 | readable | — |

The negative control matters: phase 2 being *unreadable* at 115200 is what distinguishes a real
divisor change from a call that returned 0 and did nothing.

### In-tree suites

`uart_basic_api` is declared `harness: keyboard`, so twister cannot run it — it waits for a human to
type. I supplied the keystrokes over the same serial line:

```
SUITE PASS - 100.00% [uart_basic_api]: pass = 6, fail = 0, skip = 0
 - test_uart_config_get      - test_uart_configure
 - test_uart_fifo_fill       - test_uart_fifo_read
 - test_uart_poll_in         - test_uart_poll_out
SUITE PASS - 100.00% [uart_basic_api_pending]: pass = 1, fail = 0, skip = 0
 - test_uart_pending
PROJECT EXECUTION SUCCESSFUL
```

`uart_interrupt_api` also passes (`test_uart_fifo_tx_sizes`), though note upstream scopes that suite
`vendor_allow: adi`, so it is not a test our board is expected to run and its passing is a bonus
rather than a gate.

The remaining suites in `tests/drivers/uart/` are async-API, emul or dual-UART tests; the driver
implements the interrupt-driven API, not the async one, and the board exposes a single console UART,
so they are not applicable rather than untested.

### Cell restart cycling

Six shutdown → destroy → create → load → start cycles. Six clean boots, correct board string every
time, no failed CPUs, no jailhouse errors. That closes the last row of your matrix.

## Found: two dead lines in both board defconfigs

**This is pre-existing and the only actionable defect in this report.** It fires on a plain
`hello_world` build, so it is not an artifact of my test images.

```
warning: DCACHE_LINE_SIZE_DETECT ... was assigned the value 'y' but got the value 'n'.
  Check these unsatisfied dependencies: DCACHE_LINE_SIZE_DETECT_SUPPORT (=n)
warning: ICACHE_LINE_SIZE_DETECT ... was assigned the value 'y' but got the value 'n'.
  Check these unsatisfied dependencies: ICACHE_LINE_SIZE_DETECT_SUPPORT (=n)
```

Both board defconfigs, line 8 and 9, in `GENIO: boards: mediatek: add MT8390 Genio 700 EVK` and its
510 counterpart:

```
boards/mediatek/mt8390_genio_700_evk/mt8390_genio_700_evk_mt8188_a55_defconfig:8,9
boards/mediatek/mt8370_genio_510_evk/mt8370_genio_510_evk_mt8188_a55_defconfig:8,9
```

arm64 does not implement runtime cache-line-size detection, so `*_LINE_SIZE_DETECT_SUPPORT` is `n`
and both symbols are forced off. The values land on the static defaults anyway:

```
CONFIG_DCACHE_LINE_SIZE=64
CONFIG_ICACHE_LINE_SIZE=64
```

which is correct for Cortex-A55, so **nothing is broken** — but the two lines have no effect and emit
a warning on every single build. Upstream review will flag it. Suggest deleting both lines from both
defconfigs; `CONFIG_CACHE_MANAGEMENT=y` on line 7 is doing the real work and should stay.

I have not made the change — it edits two of your commits, and per the standing rules that is a
fold-into-commit request rather than something I should improvise. Say the word and I will fold it
into commits 14 and 15.

## Board state

`/root/claude_aary` on the Genio 700, all verified by md5 after transfer:

| File | Purpose | md5 |
|---|---|---|
| `zephyr-g700.bin` | plain `hello_world` | `8ab837b80144f4f943decef7d970e4e1` |
| `zephyr-g700-cntfrq.bin` | `cntfrq` + three 5 s sleeps | `2f3f30bebfcb5019d53b00cd71317f8e` |
| `zephyr-g700-rxtest.bin` | interrupt-driven echo + counters | `eaae32fc1279bc50329c924a42ca5931` |
| `zephyr-g700-reconf.bin` | runtime baud change | `915129ee4d4b5a0e09a58eccc57a23d1` |
| `zephyr-g700-uartapi.bin` | `tests/drivers/uart/uart_basic_api` | `823337971d33a1e6b36dc780ac6bebad` |
| `zephyr-g700-uartirq.bin` | `tests/drivers/uart/uart_interrupt_api` | `8fd93b83688e266f4e14120ebd5013f5` |
| `zephyr-v442-g700.bin` | `mtk-v4.4.2` boot check | `9c4e65f642c170d0138c46326cb14f8c` |

Board left running `zephyr-g700.bin`. Host-side test scripts are in `scripts/` on this branch.

## Matrix — complete for the Genio 700

| Test | Genio 700 | Genio 510 |
|---|---|---|
| Boots to UART1 console at 115200 | PASS | PASS |
| `hello_world` prints the right board target | PASS | PASS |
| `cntfrq` = 13 MHz | PASS | PASS |
| `k_sleep(K_SECONDS(5))` wall-clock accurate | PASS | PASS |
| UART TX **and** RX | **PASS** | not run |
| Runtime UART reconfigure | **PASS** | not run |
| `tests/drivers/uart/*` | **PASS** (applicable suites) | not run |
| Interrupt delivery under load | **PASS** (10,065 interrupts, zero loss) | not run |
| Cell shuts down and restarts cleanly | **PASS** (6/6) | not run |
| `mtk-v4.4.2` GIC restore boots | PASS (native) | PASS (cross-board) |

The 510 rows marked "not run" are the new tests; it passed everything that existed when it was on the
bench. The images and scripts are branch-independent, so re-running them there is a short job
whenever it is reconnected — worth doing before the customer drop, since the 510 is the part that
ships.

## Not attempted

**SMP / multiple A55 cores.** Deferred by Aary. It needs the Jailhouse inmate cell to assign more
than one CPU, and cell configs live outside the Zephyr tree, so it is a MediaTek-side change first.
The board dts disabling five of six A55s and `CONFIG_MP_MAX_NUM_CPUS=1` are the Zephyr-side pieces.
