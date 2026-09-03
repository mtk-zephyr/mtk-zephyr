# /root/claude_aary — Zephyr on Genio, build-machine notes

Left here 2026-09-02 by the build-machine agent. This board is a **Genio 510 EVK**
(hostname genio-510-evk).

## Images

| File | Branch | Board it was built for | md5 |
|---|---|---|---|
| zephyr-g510.bin | mtk-genio-dev bfaca73809d | mt8370_genio_510_evk | 1b218ad402b0ea77eb56b9edb743c114 |
| zephyr-g700.bin | mtk-genio-dev bfaca73809d | mt8390_genio_700_evk | 646cbf1c9d4b32d5a6759f9b4b908ff0 |
| zephyr-g510-cntfrq.bin | mtk-genio-dev bfaca73809d | mt8370_genio_510_evk | 5abb17a0f24303edd31189ea9ed124e5 |
| zephyr-v442-g700.bin | mtk-v4.4.2 c4333dd7d9c | mt8390_genio_700_evk | 9c4e65f642c170d0138c46326cb14f8c |
| zephyr.bin | (pre-existing) | = zephyr-g700.bin | 646cbf1c9d4b32d5a6759f9b4b908ff0 |

Always check the md5 before drawing conclusions, and quote it in any report.
The boot banner also carries the source SHA, e.g. "build v4.4.2-18-gc4333dd7d9c2".

## Running

    ./setup-g510.sh [image]        # default zephyr-g510.bin

Tears down any existing zephyr cell first, then creates the 510 inmate cell,
loads at 0x00008000 and starts. Console is UART1 at 115200 on CN3201.

## Careful: the original setup.sh is wrong for this board

setup.sh enables the genio-510-evk root cell (correct) but then creates
genio-700-evk-zephyr.cell and loads the genio-700 image. It boots, because the
parts are pin-compatible, but the banner then reports the 700 board on 510
hardware. setup.sh is left unmodified; use setup-g510.sh instead.

## Already verified on this board (build machine, 2026-09-02)

- Genio 510 boots its own image in its own cell: banner reads
  mt8370_genio_510_evk/mt8188/a55
- cntfrq = 13000000 (13 MHz) -> confirms SYS_CLOCK_HW_CYCLES_PER_SEC
- k_sleep(K_SECONDS(5)) measured 5.004-5.005 s against a host wall clock ->
  confirms the IRQ_TYPE_LEVEL arch-timer fix
- mtk-v4.4.2 image (zephyr-v442-g700.bin) boots -> confirms the GIC MMU restore

## Not yet tested

UART RX, runtime reconfigure (UART_USE_RUNTIME_CONFIGURE), tests/drivers/uart/*,
interrupt delivery under sustained load, cell shutdown/restart cycling, and the
Genio 700 board itself (not connected).

## Measuring sleep accuracy

k_uptime_get() derives from the timer under test, so it cannot detect a
misconfigured one. The sample prints SLEEP_START/SLEEP_END markers and the host
timestamps their arrival. Note the very first line after boot can arrive in a
burst once the console comes up, which makes the first interval read short;
trust the second and third.
