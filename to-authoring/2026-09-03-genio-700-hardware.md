# Genio 700 EVK — hardware results (2026-09-03)

The Genio 700 is now connected and has run the same set the Genio 510 passed. **Everything passes,
and two things are confirmed that only this board could confirm.**

All results below are against `mtk-genio-dev` `0d9156ec50b` — the pushed tip, so this evidence
attaches to the submitted shape.

## The six-core dtsi change is now positively confirmed

```
Genio 700 (MT8390):  6x Cortex-A55 (0xd05) + 2x Cortex-A78 (0xd41) = 8 CPUs
Genio 510 (MT8370):  4x Cortex-A55 (0xd05) + 2x Cortex-A78 (0xd41) = 6 CPUs
```

You derived "MT8390 has six and MT8370 four" from the public datasheet, and the 510 could only
corroborate the *four* half. The 700 supplies the other half directly. Both binnings now match the
silicon, so `dts/arm64/mediatek/mt8188.dtsi` describing six A55 cores — with the board dts disabling
`cpu@400` and `cpu@500` for the 510 — is right for both parts.

## Results

| Test | Genio 700 | Genio 510 (for comparison) |
|---|---|---|
| Boots its own image in its own cell | **PASS** — `Hello World! mt8390_genio_700_evk/mt8188/a55` | PASS |
| Banner carries the source SHA | **PASS** — `v4.4.0-13857-g0d9156ec50b9` | PASS |
| `cntfrq` | **13000000** | 13000000 |
| `k_sleep(K_SECONDS(5))` vs host wall clock | **5.004 / 5.005 / 5.012 s** | 5.004 s |
| `mtk-v4.4.2` image boots | **PASS, natively** — `v4.4.2-18-gc4333dd7d9c2`, 5.005 / 5.004 / 5.004 s | PASS (cross-board) |

`cntfrq` reading 13 MHz on both parts is worth more than it did from one board: the value is not a
510-specific binning artifact. `SYS_CLOCK_HW_CYCLES_PER_SEC = 13000000` stands for both.

### The v4.4.2 GIC restore is now proven natively

On the 510 I could only test it cross-board — the v4.4.2 branch has no Genio 510 board, so I ran the
700 image on 510 hardware and argued the GIC entries are a SoC-level change. That argument is no
longer needed: the 700 image now runs on the 700, with three MMU entries, and boots. The customer
branch is proven on the hardware it was built for.

## New for the docs: `jailhouse enable` IS required

One of the six remaining `TODO(` items asks whether `jailhouse enable` is needed or the image brings
the root cell up. **On the Genio 700 it is required.** At power-on the module is not loaded and no
root cell exists:

```
lsmod | grep jailhouse   -> not loaded
jailhouse cell list      -> empty
```

so the sequence is `modprobe jailhouse`, `jailhouse enable
/usr/share/jailhouse/cells/genio-700-evk.cell`, then create/load/start the inmate cell. That closes
`TODO(6)` on the 700; the 510 arrived with jailhouse already enabled, so please confirm the same
holds there from cold before writing it into both docs as unconditional.

### A trap worth documenting alongside it

**`jailhouse cell list` exits 0 even when jailhouse is not enabled** — it simply prints nothing. A
guard written as `jailhouse cell list >/dev/null 2>&1 || jailhouse enable ...` therefore skips the
enable, and the failure surfaces two commands later as:

```
JAILHOUSE_CELL_CREATE: Invalid argument
```

which reads like a bad cell config rather than a missing root cell. I hit exactly this. The fix is to
test for a cell row, `jailhouse cell list 2>/dev/null | grep -qE '^[0-9]'`. Worth a line in the doc,
because the error message points away from the actual cause.

## Multiple A55 cores — not supported as built

Aary asked about this for later. Three things would have to change together, and only one of them is
in the Zephyr tree:

| | |
|---|---|
| Jailhouse inmate cell | assigns exactly one CPU (3). Cell configs live **outside** the Zephyr tree. |
| Board dts | disables `cpu@0`, `@100`, `@200`, `@400`, `@500`, leaving only `cpu@300`. |
| Kconfig | `CONFIG_MP_MAX_NUM_CPUS=1`, `CONFIG_SMP` not set. |

The dtsi already describes all six A55 cores, so the devicetree is the least of it. The cell config
is the awkward part: it is MediaTek's, not ours, so enabling SMP would need a matching cell that
hands Zephyr more than one CPU. Not attempted, and nothing here is blocked on it.

## Board state

`/root/claude_aary` on the Genio 700:

| File | Branch | md5 |
|---|---|---|
| `zephyr-g700.bin` | `mtk-genio-dev` `0d9156ec50b` | `8ab837b80144f4f943decef7d970e4e1` |
| `zephyr-g700-cntfrq.bin` | `mtk-genio-dev` `0d9156ec50b` | `2f3f30bebfcb5019d53b00cd71317f8e` |
| `zephyr-v442-g700.bin` | `mtk-v4.4.2` `c4333dd7d9c` | `9c4e65f642c170d0138c46326cb14f8c` |

`README.md` and `setup-g700.sh` alongside. The board is left running `zephyr-g700.bin`. Console is
UART1 at 115200 on a different FTDI adapter than the 510 (`AB0PKARP` vs `B001I8ZJ`), same
`/dev/ttyUSB0`.

## Still untested on either board

UART RX (nothing has exercised the receive path), runtime reconfigure,
`tests/drivers/uart/*`, interrupt delivery under sustained load, and deliberate cell
shutdown/restart cycling. Those are next.
