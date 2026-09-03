# /root/claude_aary — Zephyr on Genio 510 EVK

Updated 2026-09-03 by the build-machine agent. Board: **Genio 510 EVK**
(genio-510-evk), 4x Cortex-A55 + 2x Cortex-A78 = 6 CPUs. Zephyr gets CPU 3.

## Running

    ./setup-g510.sh [image]        # default zephyr-g510.bin

Console UART1 115200 on CN3201. Loads at 0x00008000 into
genio-510-evk-zephyr.cell.

**jailhouse enable IS required from cold on this board.** The script does
modprobe + enable before creating the inmate cell. An earlier version of this
script omitted the enable and worked only because jailhouse happened to be
running; after a power cycle it failed with
"JAILHOUSE_CELL_CREATE: Invalid argument", which points away from the real cause.
Note "jailhouse cell list" exits 0 even when jailhouse is disabled, so the guard
greps for an actual cell row rather than testing the exit code.

## Images (all built from mtk-genio-dev 6e09950bf79)

| File | Purpose |
|---|---|
| zephyr-g510.bin | plain hello_world |
| zephyr-g510-cntfrq.bin | cntfrq + three 5 s sleeps |
| zephyr-g510-rxtest.bin | interrupt-driven echo + rx/isr counters |
| zephyr-g510-reconf.bin | runtime baud change 115200 -> 9600 -> 115200 |
| zephyr-g510-uartapi.bin | tests/drivers/uart/uart_basic_api |
| zephyr-g510-uartirq.bin | tests/drivers/uart/uart_interrupt_api |
| zephyr-g700.bin, zephyr-v442-g700.bin | older 700-board images, kept for reference |

Check md5 against the host build and quote it in any report; the boot banner also
carries the source SHA.

## Verified on this board 2026-09-03 — all pass

- boot, correct board string
- cntfrq = 13000000
- k_sleep(5 s) = 5.005 / 5.004 / 5.004 s by host wall clock
- RX echo byte-for-byte at 64, 2000, 8000 bytes
- interrupt load: rx=10065 isr=10065, zero bytes lost
- runtime reconfigure verified on the wire, incl. unreadable-at-old-rate control
- uart_basic_api 7/7, uart_interrupt_api 1/1
- 6/6 cell shutdown/restart cycles

Identical results to the Genio 700.

## Not tested

SMP / multiple A55 cores. Needs a Jailhouse cell assigning more than one CPU;
cell configs live outside the Zephyr tree.

## Two traps

1. "No console output" is both a real failure signature and what a dead host-side
   logger looks like. Check fuser /dev/ttyUSB0 before treating silence as a result.
2. k_uptime_get() derives from the timer under test. Use host-side timestamps.
