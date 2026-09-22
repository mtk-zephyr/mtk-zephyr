# PR C: the AFE works on hardware — with two platform requirements nobody had written down

Genio 510 EVK (MT8370), Genio 700 image built `-S mtk-afe`, run under
`genio-700-evk-zephyr-afe.cell`. Same die family, same AFE addresses, identical
inmate window and console, so the 700-authored cell is used unchanged; only the
root cell comes from the board.

**All three loopbacks pass.** Frame-by-frame verification, 19 seconds each, not
one mismatch:

| Sample | Channels | Played | Verified | |
|---|---|---|---|---|
| `mt8188_loopback_dl11_ul8` | 16, eTDM1 pair | 925,920 | 921,120 | `rot 0`, `lag -1` |
| `mt8188_loopback_dl8_ul3` | 16, eTDM2 via the mux | 925,920 | 921,120 | `rot 0`, `lag 30` |
| `mt8188_loopback_dl11_ul9` | 32, cowork pair + CM0 | 928,320 | 923,520 | identity slot map |

Lag is constant in each and differs between them, which is what different
pipeline depths should look like. The 32-channel run printed its slot to channel
map as a clean 0-31, so the co-clocked pair and the merge unit both route
correctly. Console transcripts in
`artifacts/uart1-genio510-etdm-loopback-2026-09-21.log`.

That covers **C1 and C3 through C7**. **C2** and **C10** pass as the suite's
9/9. C8 and C9 are new samples, below.

## Two requirements the driver cannot satisfy on its own

Both took a day to find because both fail silently, and both belong in the
documentation whatever else happens to this series.

### 1. Linux has to hold the AFE's power domain up

The AFE sits in the MTCMOS `audio` domain. It is **off on a stock boot**, and
neither the cell nor the driver can raise it: no SPM region is granted, and
enabling clocks is a different mechanism from power gating. Before the cell
steps, on the Linux side:

    echo on > /sys/devices/platform/soc/10b10000.afe/power/control

Measured, so it is not a guess:

    audio  off-0   fresh boot, mt8188-audio bound
    audio  off-0   driver unbound
    audio  on      only while a Linux stream is actually running
    audio  off-0   immediately when that stream ends, and every second to +10s

Unbinding Linux's driver does **not** work — the domain tracks an active
stream, not whether the driver is loaded — and there is no lingering window to
race a cell start against. Without this, every AFE register write is dropped
while `configure()`, `route()`, `set_buf()` and `start()` all return 0. The
only symptom is `AFE_DL11_CUR` reading an address outside the buffer.

### 2. The eTDM pins have to be muxed by Linux, not by the snippet

The pin controller is granted by **none** of the cells — not the plain one, not
either AFE one. Muxing goes through the Jailhouse pin controller mediator,
whose per-pin grant bitmap lives in the hypervisor build, so
`pinctrl-0 = <&etdm_default>` in the snippet applies nothing under the cell and
`pinctrl_apply_state()` returns 0 regardless.

Before the board's Linux devicetree was changed, the eight pins that need mode 3
read mode 0 or 1, and every loopback failed: DL11 ran at exactly 48 kHz while
UL8 never moved. Once Linux muxed them, everything above passed with no change
to the Zephyr side. Table in
`artifacts/genio510-etdm-pinmux-2026-09-21.txt`.

This is worth a decision rather than a note. The series ships a pin control
state that demonstrably does nothing in the configuration it is built for, and
that is the first question a reviewer will ask.

**C4 is therefore answered by measurement**, not inferred from a clean boot:
sixteen pins read the exact mode the pin control state asks for, checked before
anything ran.

## One open defect

**The AFE cannot re-initialise after a full teardown.** Reproducible, six for
six: the inmate that runs after one which stopped all its streams hangs during
AFE init. The console stops after the sample's own header, before the first
driver call returns; the cell stays `running` with its CPU spinning, and
destroying it takes the root cell down with it. Only a board reboot clears it.

    fresh boot -> C8 passes  ->  next run hangs
    reboot     -> C8 fails (negative control, by design)  ->  next run hangs
    reboot     -> C8 passes  ->  next run hangs

The three loopbacks never hit it because they never stop — they run until the
cell is destroyed under them, so the domain is never released. Only a sample
that calls `stop()` on its last stream leaves the AFE this way.

It is the mirror of what C8 tests: C8 proves the count is right while a domain
is *shared*; this is the count correctly reaching zero and the hardware not
surviving it. Something in the APLL1, tuner and a1sys teardown is not reset by
the next `init()`. Practical effect: stop audio, restart the inmate, and it
wedges — an ordinary thing for a deployment to do.

Not attempted here. It is in the init and teardown path, and diagnosing it
means reading register state across the boundary rather than guessing.

## The installed cell is not the one in your checkout

    board   /usr/share/jailhouse/cells/genio-700-evk-zephyr-afe.cell  664 B  md5 1417a3b2
    yours   jailhouse-cells/genio-700-evk-zephyr-afe.cell             632 B  md5 3ee805c1

32 bytes is exactly one `struct jailhouse_memory`. The installed cell grants one
region yours does not: **`0x10001400`, 3 KB at offset 0x400 inside the
`infracfg_ao` page, ROOTSHARED**. Everything else matches — topckgen,
apmixedsys, `0x10007000`, the AFE, `0x10b91000`, the console UART, the 8 MB
buffer region at `0x61000000` (which is Linux's own `snd-dma-mem-region`), the
image window and the comm page.

Your §2a said to trust the running image over the checkout. That was right.

## Fixes folded in

Five into the commits that own the lines:

| Fix | Commit |
|---|---|
| `IRQ_TYPE_LEVEL_HIGH` to `IRQ_TYPE_LEVEL` — the Linux spelling broke the plain build | AFE driver |
| `#undef CLEAR_TBL` moved after its last use | AFE driver |
| unused `int ret` removed | AFE driver |
| `__subsystem` on `mt8188_afe_driver_api` — the `DEVICE_API()` fix linked as an orphan section, so the check passed while the effect did not happen | AFE driver |
| two undefined Kconfig symbols in all eight samples, and a `target_include_directories` pointing at a directory this series deletes | samples |

The samples had never been compiled. `CONFIG_AUDIO_MEDIATEK_MT8188_AFE` and
`CONFIG_CLOCK_CONTROL_MT8188` do not exist; the real symbols are
`AUDIO_MT8188_AFE` and the three per-block `CLOCK_CONTROL_MT8188_*`, all
`default y` on devicetree presence, so the whole fragment reduces to
`CONFIG_AUDIO=y`. None of the eight has a `tests.yaml`, so twister never builds
them, which is why this survived. Adding that metadata would close the hole.

## Two new samples

**`mt8188_twostream_dl11_dl8`** (C8). DL11 on eTDM_OUT1 and DL8 on eTDM_OUT2,
both 16 channel at 48 kHz so they hold one timing domain between them. Four
phases measured from the DMA pointer: both started, DL8 stopped, DL8 restarted,
DL11 stopped. The restart separates a count that never rises from one that never
falls; the last phase repeats it in the other order.

Checked that it can fail: releasing the domain twice in `stop()` — the pre-fix
behaviour — makes the survivor read 0 frames in phases 2 and 4 while 1 and 3
still pass. Both transcripts are in
`artifacts/uart1-genio510-afe-c8-c9-2026-09-21.log`.

**`mt8188_api_reject`** (C9). The guards on `configure()` and `set_buf()`:
out-of-range and undriven interfaces, a null configuration, zero and oversized
channel counts, an unsupported word size, a rate with no timing value, and
buffers that are zero sized, mis-sized, unaligned or past the end of the address
space. 15 of 15. Each function is also called once with valid arguments, so a
guard that has started rejecting everything is told apart from one that works.

## What is still yours to settle

- **The GPL-2.0 header on `mt8188_afe_reg.h`.** Unchanged, and still the one
  blocker no amount of board time resolves.
- **Sign-offs.** All nine commits have none; only the submitter can add them.
- **`Assisted-by:`.** Absent on your seven. The two samples written here carry
  it. `AGENTS.md` asks for exactly one per commit.
- **The pin control state question** in section 2 above.
