# /root/claude_aary — Zephyr on Genio, build-machine notes

Updated 2026-09-03 by the build-machine agent. This board is a **Genio 510 EVK**
(hostname genio-510-evk).

## Images — rebuilt for the 2026-09-03 drop (mtk-genio-dev 0d9156ec50b)

| File | Branch | Built for | md5 |
|---|---|---|---|
| zephyr-g510.bin | mtk-genio-dev 0d9156ec50b | mt8370_genio_510_evk | 501853ffa9eeb7ef2721318fca01b951 |
| zephyr-g700.bin | mtk-genio-dev 0d9156ec50b | mt8390_genio_700_evk | 8ab837b80144f4f943decef7d970e4e1 |
| zephyr-g510-cntfrq.bin | mtk-genio-dev bfaca73809d | mt8370_genio_510_evk | 5abb17a0f24303edd31189ea9ed124e5 |
| zephyr-v442-g700.bin | mtk-v4.4.2 c4333dd7d9c | mt8390_genio_700_evk | 9c4e65f642c170d0138c46326cb14f8c |
| zephyr.bin | (pre-existing, stale) | mt8390_genio_700_evk | 646cbf1c9d4b32d5a6759f9b4b908ff0 |

zephyr-g510.bin and zephyr-g700.bin were refreshed so hardware evidence attaches
to the SHAs we intend to submit. The cntfrq image is from the previous commit
shape; it differs from the current one only in the 12-hex-digit SHA embedded in
the boot banner, so its results still stand.

Always check the md5 and quote it in any report. The boot banner also carries the
source SHA, e.g. "build v4.4.0-13857-g0d9156ec50b9".

## Running

    ./setup-g510.sh [image]        # default zephyr-g510.bin

Tears down any existing zephyr cell, creates the 510 inmate cell, loads at
0x00008000, starts. Console UART1 115200 on CN3201.

## Careful: the original setup.sh is wrong for this board

It enables the genio-510-evk root cell (correct) but creates
genio-700-evk-zephyr.cell and loads the genio-700 image. It boots, because the
parts are pin-compatible, but the banner then reports the 700 board on 510
hardware. Left unmodified; use setup-g510.sh.

## Already verified on this board (build machine)

- Genio 510 boots its own image in its own cell:
  banner "Hello World! mt8370_genio_510_evk/mt8188/a55"
- cntfrq = 13000000 (13 MHz) -> SYS_CLOCK_HW_CYCLES_PER_SEC confirmed, no amend
- k_sleep(K_SECONDS(5)) = 5.004 s by host wall clock -> IRQ_TYPE_LEVEL fix confirmed
- mtk-v4.4.2 image boots -> GIC MMU restore confirmed on the customer branch

## Not yet tested

UART RX, runtime reconfigure (UART_USE_RUNTIME_CONFIGURE), tests/drivers/uart/*,
interrupt delivery under sustained load, deliberate cell shutdown/restart, and
the Genio 700 board itself (not connected).

## Two traps

1. An absent console is ambiguous. "No output" is both a real failure signature
   and what a dead host-side logger looks like. Confirm the logger holds the port
   (fuser /dev/ttyUSB0) before treating silence as a result.
2. k_uptime_get() derives from the timer under test and cannot detect a
   misconfigured one. Use host-side timestamps. The first line after boot can
   arrive in a burst once the console comes up, making the first interval read
   short; trust the second and third.
