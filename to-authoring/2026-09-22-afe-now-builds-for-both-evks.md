# The AFE now builds and runs for both Genio EVKs

Until now `-S mtk-afe` worked only on the Genio 700. Three things stopped the
510, none of them in the driver:

- `snippet.yml` matched `/.*mt8390_genio_700_evk.*/` only. On the 510, west
  accepted `-S mtk-afe`, printed `Snippet(s): mtk-afe`, and applied **nothing** —
  no `dma_region`, `&afe` left disabled. The build then failed much later, at
  compile, with `'__device_dts_ord_5' undeclared`.
- `etdm_default` lived in the 700's board pin control file, so the snippet's
  `pinctrl-0 = <&etdm_default>` had nothing to bind to on the 510.
- The 510's board yaml did not list `audio`, so twister would never select it
  for audio tests.

## What changed

The eTDM pin control state moved to
**`boards/mediatek/common/genio-evk-pinctrl-common.dtsi`**, which both boards
include. The two EVKs are reference designs around the same die and route the
audio serial pins identically, so one copy describes both. `boards/<vendor>/common/`
is the established place for this — `boards/raspberrypi/common/rpi_pico-pinctrl-common.dtsi`
is included by two boards the same way.

The snippet now matches both EVKs, the 510 yaml lists `audio`, and the 510 doc
gained an Audio section. The commit subject changed with it, from "for the Genio
700 EVK" to "for the Genio EVKs".

**The move was verbatim** — the removed block and the new file's block are
identical character for character.

**And it is a no-op for the 700, proved rather than argued.** The include sits
*after* each board's own states, because node order decides `DT_FOREACH_CHILD`
order and the child indices: with the include first, `etdm_default` moved from
child index 8 to earlier and thirty generated macros changed. With it last, the
700's generated devicetree is **identical before and after** — every pin control
macro, every iteration order, every child index.

## Verified

- All ten audio samples build for the Genio 510, 0 errors, 0 warnings.
- Both boards build plain and with the snippet.
- `mt8188_loopback_dl11_ul8`, **built for the 510** rather than borrowing the
  700 image, runs on the 510 hardware: `rot 0`, constant lag, every captured
  frame matching. Previously this had only been shown with a 700-built image.
- 32 compliance checks pass. The two that do not are the missing sign-offs and a
  pre-existing ClangFormat finding on `err_gate:` in `mt8188_afe.c`, which is
  yours and which I have left alone.

## Two notes

**The uart and gpio states are still duplicated** between the two board files —
they were already identical before any of this, 27 lines of it. They can move
into the same shared file, but the commits that introduce them belong to the
series currently under review, so that is a job for after it merges, not now.

**This does not make the 510 run audio on its own.** Two things remain outside
the Zephyr tree: a 510 AFE cell (we reused the 700's, which works because the
inmate window, console and AFE addresses are identical), and the board's Linux
devicetree muxing the eTDM pins. The snippet's pin control state still applies
nothing under the hypervisor — see the earlier note.

## Refinement to the re-init defect

It only affects runs that **start** a stream. `mt8188_api_reject` runs fine
immediately after a teardown because it never starts one. That narrows where to
look: the fault is in bring-up, not in anything the driver does at init.
