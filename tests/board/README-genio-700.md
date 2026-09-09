# /root/claude_aary — Zephyr on Genio 700 EVK

Set up 2026-09-03 by the build-machine agent. This board is a **Genio 700 EVK**
(hostname genio-700-evk): 6x Cortex-A55 + 2x Cortex-A78 = 8 CPUs.

## Images

| File | Branch | md5 |
|---|---|---|
| zephyr-g700.bin | mtk-genio-dev 0d9156ec50b | 8ab837b80144f4f943decef7d970e4e1 |
| zephyr-g700-cntfrq.bin | mtk-genio-dev 0d9156ec50b | 2f3f30bebfcb5019d53b00cd71317f8e |
| zephyr-v442-g700.bin | mtk-v4.4.2 c4333dd7d9c | 9c4e65f642c170d0138c46326cb14f8c |

Always check the md5 and quote it in any report. The boot banner also carries the
source SHA.

## Running

    ./setup-g700.sh [image]        # default zephyr-g700.bin

Loads at 0x00008000 into genio-700-evk-zephyr.cell. Console UART1 115200 on CN3201.

**jailhouse enable IS required on this board.** The module is not loaded and no
root cell exists at power-on, so the script does modprobe + enable before creating
the inmate cell. That answers the doc TODO about whether the image brings the root
cell up: it does not.

Note "jailhouse cell list" exits 0 even when jailhouse is not enabled, so a guard
based on its exit code silently skips the enable and the later cell create fails
with "JAILHOUSE_CELL_CREATE: Invalid argument". The script greps for an actual
cell row instead.

## Verified on THIS board (2026-09-03)

- Boots its own image: banner "Hello World! mt8390_genio_700_evk/mt8188/a55"
- cntfrq = 13000000 (13 MHz), same as the Genio 510
- k_sleep(K_SECONDS(5)) = 5.004 / 5.005 / 5.012 s by host wall clock
- mtk-v4.4.2 image boots natively here: 5.005 / 5.004 / 5.004 s

## Not yet tested

UART RX, runtime reconfigure, tests/drivers/uart/*, interrupt delivery under
sustained load, deliberate cell shutdown/restart cycling.

## Multiple A55 cores — not supported as built

Three things would all have to change together:

1. The Jailhouse inmate cell assigns exactly one CPU (3). Cell configs live
   outside the Zephyr tree.
2. The board dts disables cpu@0, @100, @200, @400, @500, leaving only cpu@300.
3. CONFIG_MP_MAX_NUM_CPUS=1 and CONFIG_SMP is not set.

The dtsi already describes all six A55 cores, so the devicetree side is the least
of it.

## Two traps

1. An absent console is ambiguous: "no output" is both a real failure signature
   and what a dead host-side logger looks like. Confirm the logger holds the port
   (fuser /dev/ttyUSB0) before treating silence as a result.
2. k_uptime_get() derives from the timer under test and cannot detect a
   misconfigured one. Use host-side timestamps.
