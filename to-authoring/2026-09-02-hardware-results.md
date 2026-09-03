# Hardware results — build machine (2026-09-02)

First hardware run. **Both of the open questions you flagged are answered, and both of the
silent-failure changes are now proven on real hardware.** No code changed; no commit amended.

## What was on the bench

The connected board is a **Genio 510 EVK** — not a 700. The Genio 700 has never been connected.
Everything below therefore runs on 510 hardware.

```
hostname genio-510-evk
4x Cortex-A55 (0xd05) + 2x Cortex-A78 (0xd41) = 6 CPUs
Zephyr holds CPU 3 (an A55); Linux holds 0-2, 4-5
```

That core count independently corroborates your datasheet reasoning: **MT8370 has four A55**, which
is the premise behind the six-core dtsi change. You derived it from a datasheet; the silicon agrees.

## Answers to your two open questions

### `cntfrq = 13000000`

Read at boot on both branches. `SYS_CLOCK_HW_CYCLES_PER_SEC = 13000000` in
`GENIO: soc: mediatek: mt8188: add the a55 cpucluster` is **correct — no amendment needed.**

### Timer accuracy — the `IRQ_TYPE_LEVEL` fix is confirmed

`k_sleep(K_SECONDS(5))` measured against a **host** wall clock, since `k_uptime_get()` derives from
the very timer under test and cannot detect a misconfigured one:

| Branch | iter 0 | iter 1 | iter 2 |
|---|---|---|---|
| `mtk-genio-dev` `bfaca73809d` | 4.003 s* | 5.004 s | 5.004 s |
| `mtk-v4.4.2` `c4333dd7d9c` | 5.005 s | 5.004 s | 5.004 s |

4 ms over 5 s is 0.08%, and is accounted for by the `printk` and UART transmission themselves. The
arch-timer PPIs are level-triggered correctly.

\* **Not a defect.** On that one run every early-boot line — banner, hello, `cntfrq`, `SLEEP_START 0`
— arrived within 1 ms of each other, i.e. flushed as a single burst once the console came up, so the
first marker's *arrival* stamp lagged its emission by about a second. The v4.4.2 run shows no such
burst and its iteration 0 is clean. Flagging it because a single first-iteration sample would read
as a 20% timer error, and it is not one.

## The `mtk-v4.4.2` GIC MMU restore boots — this was the risky one

```
*** Booting Zephyr OS build v4.4.2-18-gc4333dd7d9c2 ***
Hello World! mt8390_genio_700_evk/mt8188/a55
cntfrq = 13000000
```

Built from `mtk-v4.4.2` (`c4333dd7d9c`, `mmu_regions.c` with 3 entries), loaded at `0x8000` in
`genio-700-evk-zephyr.cell`. v4.4.2 has only the Genio 700 board, but the GIC entries are a
SoC-level change and the 700 image is known to run on 510 hardware, so this is a valid test of the
change itself. Held the cell and load address constant against a known-booting configuration so the
only variable was the branch.

Its failure mode was a silent fault before the console exists. It does not fault. **The customer
branch boots.**

## A near-miss worth recording

The first v4.4.2 attempt produced **no output at all** — which is exactly what a failed GIC mapping
looks like. It was not: my host-side UART logger had been killed, so nothing was capturing. `fuser
/dev/ttyUSB0` showed no holder and the background task was marked `[killed]`.

Had I reported that reading, it would have looked like hard evidence that the fix does not work, and
you would have gone rewriting a correct commit. **Verify the logger holds the port before trusting an
absence of output** — on this platform "no console output" is a legitimate result *and* the signature
of a dead capture, and the two are indistinguishable from the log alone.

## Correction to something I proposed

I suggested the boot banner carries the source SHA as provenance. It does — but **my own
`verified/2026-09-02` tag broke it** on `mtk-genio-dev`. Zephyr's version string comes from
`git describe`, which prefers annotated tags:

```
git describe                        -> verified/2026-09-02
git describe --exclude='verified/*' -> v4.4.0-13857-gbfaca73809d
```

So images built after I tagged report `build verified/2026-09-02` with no SHA, while the v4.4.2
image (untagged commit) correctly reports `v4.4.2-18-gc4333dd7d9c2`. Fix is to make `verified/*`
lightweight tags, which `git describe` ignores by default, keeping both the tag and the SHA. Until
then the md5 remains the reliable identifier.

## Images left on the board, in `/root/claude_aary`

| File | Branch | Built for | md5 |
|---|---|---|---|
| `zephyr-g510.bin` | `mtk-genio-dev` `bfaca73809d` | `mt8370_genio_510_evk` | `1b218ad402b0ea77eb56b9edb743c114` |
| `zephyr-g700.bin` | `mtk-genio-dev` `bfaca73809d` | `mt8390_genio_700_evk` | `646cbf1c9d4b32d5a6759f9b4b908ff0` |
| `zephyr-g510-cntfrq.bin` | `mtk-genio-dev` `bfaca73809d` | `mt8370_genio_510_evk` | `5abb17a0f24303edd31189ea9ed124e5` |
| `zephyr-v442-g700.bin` | `mtk-v4.4.2` `c4333dd7d9c` | `mt8390_genio_700_evk` | `9c4e65f642c170d0138c46326cb14f8c` |

`README.md` and `setup-g510.sh` are alongside them. The board is left running `zephyr-g510.bin` in
`genio-510-evk-zephyr.cell`.

**`setup.sh`, which was already there, is wrong for this board** — it enables the 510 root cell but
then creates `genio-700-evk-zephyr.cell` and loads the 700 image. It boots, because the parts are
pin-compatible, but the banner then reports the 700 board on 510 hardware. I left it unmodified and
added `setup-g510.sh` beside it. This is precisely why the board-string check matters: a passing boot
proved the wrong thing until the banner was read.

## Scripts, in `scripts/` on this branch

| File | Purpose |
|---|---|
| `hwtest-main.c` | drop-in `samples/hello_world/src/main.c`: prints `cntfrq`, then three `SLEEP_START`/`SLEEP_END` pairs |
| `uartlog.py` | host-side logger, timestamps each line on arrival — the external clock the sleep test needs |
| `setup-g510.sh` | board-side: tear down, create the 510 cell, load at `0x8000`, start |

## Test matrix, updated

| Test | Genio 700 | Genio 510 |
|---|---|---|
| Boots to UART1 console at 115200 | not connected | **PASS** |
| `hello_world` prints the right board target | not connected | **PASS** |
| `cntfrq` = 13 MHz | not connected | **PASS** |
| `k_sleep(K_SECONDS(5))` wall-clock accurate | not connected | **PASS** (5.004 s) |
| v4.4.2 GIC restore boots | not connected | **PASS** (700 image on 510 hw) |
| UART TX **and** RX | not connected | not run |
| Runtime UART reconfigure | not connected | not run |
| `tests/drivers/uart/*` | not connected | not run |
| Interrupt delivery under sustained load | not connected | not run |
| Cell shuts down and restarts cleanly | not connected | partially — cells were destroyed and recreated many times without incident, but not as a deliberate test |

RX is untested because `hello_world` never reads; it proves TX only. A sample that reads is needed.

## What I would prioritise next

1. **UART RX**, since nothing has exercised the receive path at all.
2. **Interrupt delivery under sustained load** — same root cause as the timer fix, on the UART SPI.
3. **The Genio 700 board**, once connected. Everything above is 510 silicon; the 700 has six A55
   cores rather than four, so the dtsi's extra cores are exercised only there.
