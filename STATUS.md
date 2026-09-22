# STATUS — as of 2026-09-22 (PR C validated on hardware; work branches on PR A review round 2)

Current state of the MediaTek Genio work on `github.com/mtk-zephyr/mtk-zephyr`. This file is
overwritten on every update; `git log` on this branch is the history.

## PR A is upstream

**PR A is pushed** — one PR, no RFC.

**Review feedback has to land in two places.** The PR branch is what reviewers see;
`mtk-genio-dev` is what everything else is generated from. Their shas do not match, and fixing
one does not fix the other.

**`mtk-genio-dev` now carries PR A review round 1**, which overrides the rule that PR A review
fixes stay on the PR fork until PR A is accepted. It is not a preference: PR B kernel panics on
the pre-review base, because `mmu_regions.c` there flat-maps the pin controller window and the
GPIO bank maps the same range again. Round 1 deleted that file. See
`to-authoring/2026-09-18-prb-needs-round1-base.md`.

PR A itself is at **review round 2** on the live PR, so `mtk-genio-dev` is one round behind and
needs another sync once PR A settles.

## PR B GPIO and EINT — verified on both parts

19/19 on the Genio 700 (MT8390) and the Genio 510 (MT8370), with a jumper between
GPIO 38 and GPIO 40. Driving pin 6 in software and sensing pin 8 gives exact event
counts, which is what proves the both-edges emulation is not inverted — 4 rising and
4 falling for 4 of each. Detail in
`to-authoring/2026-09-19-prb-gpio-verified-both-boards.md`.

The suite runs this as **H9** behind `--gpio`, off by default because the jumper is
not always fitted. Runs now write to `logs/<timestamp>-<agent>-<branch>/` so the two
agents sharing this workspace cannot overwrite each other's console transcripts.

**The upstream `tests/drivers/gpio/gpio_basic_api` also passes on both boards**, as
**H10** under the same flag. It covers what H9 deliberately does not — callback
add/remove, enable/disable, and removing a callback from inside itself. Two of its
tests skip themselves because the driver returns `-ENOTSUP` for open-drain, which
lives in the pin controller on this SoC; that is the test's own documented path.
Overlays naming the two pins ship with the series under
`tests/drivers/gpio/gpio_basic_api/boards/`.

**Copyright is settled at 2026.** Every MediaTek file the series adds reads
`Copyright (c) 2026 MediaTek Inc.`, normalised inside the commit that adds it.
Files held by other parties — the ChromiumOS notices under `soc/mediatek` — are
deliberately untouched.

## PR C — the Audio Front End works on hardware

Nine commits on `mtk-genio-dev-afe`, a temporary branch off `mtk-genio-dev`. It
is deleted once the feature settles and the work merges down; until then treat it
the way `mtk-genio-dev` is treated — **it is force-pushed, so hard reset.**

**All three eTDM loopbacks pass on the Genio 510**, frame by frame, 19 seconds
each, with no mismatches: `dl11_ul8` and `dl8_ul3` at 16 channels and
`dl11_ul9` at 32 across a cowork'd port pair, the last printing a clean 0-31
slot map. The 700-authored AFE cell is used unchanged on the 510 — same die
family, same AFE addresses, identical inmate window and console. Detail in
`to-authoring/2026-09-22-prc-afe-validated-on-hardware.md`.

**Both EVKs are supported**, not just the 700. `-S mtk-afe` previously matched
the 700 alone, and `etdm_default` lived in the 700's board file, so the 510
could not build at all — west accepted the snippet, applied nothing, and failed
later at compile. The eTDM pin control state now lives in
`boards/mediatek/common/genio-evk-pinctrl-common.dtsi` and both boards include
it; the two EVKs route the audio serial pins identically, so one copy describes
both. The include sits **after** each board's own states, because node order
decides `DT_FOREACH_CHILD` order and the child indices — with it last, the 700's
generated devicetree is identical before and after. All ten samples build for
the 510, and a 510-built loopback runs on 510 hardware.
`to-authoring/2026-09-22-afe-now-builds-for-both-evks.md`.

### Two platform requirements the driver cannot meet on its own

Both fail **silently** — every driver call returns 0 — so neither is discoverable
from a clean boot. Together they cost a day.

**1. Linux must hold the AFE's power domain up.** The AFE sits in the MTCMOS
`audio` domain, off on a stock boot, and no SPM region is granted to the inmate.
Before the cell steps:

    echo on > /sys/devices/platform/soc/10b10000.afe/power/control

Unbinding Linux's `mt8188-audio` does not work: the domain tracks an active
stream, not whether the driver is loaded, and it drops the instant a stream ends.
Without this every register write is discarded and the only symptom is a DMA
pointer outside its buffer.

**2. Linux must mux the eTDM pins.** The pin controller is granted by **none** of
the cells. Muxing goes through the Jailhouse mediator, which discards a write to
an ungranted pin and returns `MMIO_HANDLED`, so the snippet's
`pinctrl-0 = <&etdm_default>` applies nothing and `pinctrl_apply_state()` returns
0 anyway. Eight pins need mode 3 in the board's Linux devicetree; before that
change every loopback failed with no error anywhere.

This one needs a decision, not a note: the series ships a pin control state that
does nothing in the configuration it is built for.

### One open defect

**The AFE cannot re-initialise after a full teardown.** Reproducible six for six:
the inmate that runs after one which stopped all its streams hangs during AFE
init, and destroying that wedged cell takes the root cell down with it. A reboot
is the only recovery. The loopbacks never hit it because they never stop. Stop
audio, restart the inmate, and it wedges.

## Branch policy — read this before resyncing

`mtk-genio-dev` **rebases freely and is force-pushed.** It imports whatever PR A
currently is, plus the feature work on top. PR A is under review, so it is mutable, and
anything built on it inherits that. Always
`git fetch && git reset --hard origin/mtk-genio-dev`.

The distinction that matters: **re-parenting** moves your commits onto a new base
unchanged, **rewriting** alters them. Resyncs must be the first. The round 1 -> round 2
migration re-parented all six PR B commits with identical diffs *and* messages.

`pra-r2` tags the PR A tip, so the next resync is one command that preserves everything
above it:

    git rebase --onto pra-r3 pra-r2 mtk-genio-dev

Resync **on impact, not on every review comment** — the question is whether any file the
feature work modifies differs between rounds. And a clean rebase does not prove the result
runs: PR B once kernel panicked on a base it rebased onto cleanly. Re-run hardware when the
PR A delta touches SoC, pinctrl or MMU code.

`mtk-v4.4.2` is the opposite contract: **append-only, never rewritten.** Changes to earlier
work land as new commits on top, each naming what it changes as
`commit <12-char sha> ("subject")`. It takes a feature once that feature is structurally
settled, not on a schedule. It is not upstreamable — a fix-on-top history cannot produce a
clean series.

## Branch tips

| Branch | Tip | Contents | Verified |
|---|---|---|---|
| `main` | `3860b8cb663` | upstream mirror, fast-forwarded 1069 commits on 2026-09-10 | n/a |
| `mtk-genio-dev` | `a3a352da5ce` | **22 commits**: 16 PR A *review round 2* + 6 PR B. PR A portion is commit-for-commit identical to what is under review; `pra-r2` tags the boundary | gates, ADSP neutrality, 39/39 per-commit, **11/11 hardware on both boards** |
| `mtk-genio-dev-afe` | `c75fde60f20` | **9 commits** on `mtk-genio-dev`: PR C (AFE, clocks, snippet, samples) plus two samples written here. Temporary; force-pushed | C1-C10, three loopbacks frame-verified, suite 9/9 |
| `mtk-v4.4.2` | `05ef8eea7ec` | **22 commits** on `v4.4.2`: round-2 PR A + PR B, level with dev. Rebuilt cleanly — no customer is on it yet | gates 9/9, ADSP byte-identical, 39/39 per-commit, **700 11/11** |

On `github.com/mtk-zephyr/zephyr` (the upstream staging repo):

| Branch | Tip | Contents | Verified |
|---|---|---|---|
| `mtk-genio` | `bf26ae3a4c3` | **PR A only**, 15 commits on upstream `main` (`1dbf149f7dd`), `GENIO: ` stripped. **Frozen — do not rebase while under review** | 700 13/13, 510 9/9 hardware, 20/20 per-commit |

`mtk-zephyr/zephyr` is a real fork of `zephyrproject-rtos/zephyr`, so the PR is cross-repo.

Deliberately **no `Assisted-by:` trailers** on the PR A commits: they were ported from a
working repo rather than written by an agent. Future Claude-authored work carries the tag
(`doc/contribute/guidelines.rst`).

Backup tags: `pre-resync-dev` `d60c7e1f589`, `pre-resync-442` `c4333dd7d9c`,
`pre-cpu-move` `95a72658a6b`.

## mtk-v4.4.2 keeps four deliberate deltas — never reconcile them

| File | v4.4.2 keeps | why |
|---|---|---|
| `mmu_regions.c` | static `GIC_DIST`/`GIC_REDIST` entries | `arch/arm64/core/mmu.c` supplies these generically on `main` but not on 4.4.2. Dropping them is a **silent boot failure** — no console output at all |
| `uart_mtk_common.c` | `int uart_mtk_irq_update` + `return 1` | 4.4.2's `uart_driver_api` returns `int` |
| `uart_mtk_common.h` | matching `int` declaration | same |
| 700 `_defconfig` | `CONFIG_D/ICACHE_LINE_SIZE_DETECT=y` | 4.4.2-only; dev has never carried these |

Of the 52 files the series adds, exactly these four differ between the branches.

## PR A did not build on its own — fixed 2026-09-09

`cpu@400`/`cpu@500` were added by the PR B EINT/GPIO commit while the board dts that
disables them sits in PR A, so PR A alone failed dtc with "node has a unit name, but no
reg or ranges property". Hidden because every build used the full branch. The nodes moved
into `dts: arm64: mediatek: add MT8188`. Upstream requires every commit to build
(bisectability), so nothing is validated tip-only any more — `tests/tools/bisect-build.sh`.

## Suite results — 13/13 on both boards, on BOTH branches (2026-09-10)

```
gates     G1 both Arm builds   G2 compliance   G3 checkpatch
hardware  H1 boot   H2 cntfrq 13 MHz + k_sleep   H3 RX/interrupt load
          H4 reconfigure   H5 uart_basic_api   H6 uart_interrupt_api
          H7 cell cycling  H8 memory window 8 MB
sweep     every commit built individually: 29/29 on each branch
```

| | `mtk-genio-dev` | `mtk-v4.4.2` |
|---|---|---|
| Genio 700 | 13/13 | 13/13 |
| Genio 510 | 13/13 | 13/13 |
| per-commit sweep | 29/29 | 29/29 |
| image size | 156 KB | 152 KB |

`k_sleep(5s)` worst deviation 4-5 ms on every run, far inside the 100 ms tolerance.
Interrupt counters identical everywhere: `rx=10065 isr=10065`.

Each branch needs its own `west update`; building one against the other's modules
produces phantom failures. Pass `--base-ref` to match (`origin/main` for dev, `v4.4.2`
for the customer branch) or compliance spans the whole release gap.

## Hardware — both boards pass

```
Genio 700 (MT8390):  6x Cortex-A55 + 2x Cortex-A78 = 8 CPUs
Genio 510 (MT8370):  4x Cortex-A55 + 2x Cortex-A78 = 6 CPUs
```

Both binnings match the silicon, so the six-core dtsi with the 510 board disabling `cpu@400`/`@500`
is **positively confirmed on both parts**, not just inferred.

| Result | Genio 700 | Genio 510 |
|---|---|---|
| Boots its own image in its own cell | PASS | PASS |
| **`cntfrq`** | **13000000** | **13000000** |
| **`k_sleep(K_SECONDS(5))`** vs host wall clock | **5.004 / 5.005 / 5.012 s** | **5.004 s** |
| **`mtk-v4.4.2` image boots** | **PASS, natively** | **PASS, natively** (2026-09-10) |

`SYS_CLOCK_HW_CYCLES_PER_SEC = 13000000` confirmed on both parts — **no amendment needed**. The
`IRQ_TYPE_LEVEL` arch-timer fix and the v4.4.2 GIC MMU restore are both confirmed, the latter now on
the hardware it was built for.

Timer accuracy was measured against the **host**, not `k_uptime_get()` — the latter derives from the
timer under test and cannot detect a misconfigured one.

## UART and interrupt tests — all pass on BOTH boards

| Test | Genio 700 | Genio 510 |
|---|---|---|
| UART RX echo, 64 / 2000 / 8000 bytes | **PASS** byte-for-byte | **PASS** byte-for-byte |
| Interrupt load | **`rx=10065 isr=10065`** | **`rx=10065 isr=10065`** |
| Runtime reconfigure | **PASS** incl. negative control | **PASS** incl. negative control |
| `uart_basic_api` | **PASS 7/7** | **PASS 7/7** |
| `uart_interrupt_api` | **PASS 1/1** | **PASS 1/1** |
| Cell shutdown/restart cycling | **PASS 6/6** | **PASS 6/6** |

Both parts agree to the decimal on throughput (6.5 KiB/s) and within 8 ms on timing. The
`uart_basic_api`/`uart_interrupt_api` images are byte-identical between boards — those suites never
print the board string and both boards resolve to the same config.

`isr == rx` exactly: one interrupt per byte, RX FIFO trigger effectively 1. Not a bug; it does mean
RX throughput is interrupt-bound (6.5 KiB/s of an 11.5 KiB/s line rate).

The remaining `tests/drivers/uart/` suites are async-API, emul or dual-UART — the driver implements
the interrupt-driven API and the board has one console UART, so they are **not applicable** rather
than untested.

## Still not tested on hardware

- **SMP / multiple A55 cores.** Deferred. Needs a Jailhouse cell assigning more than one CPU (cell
  configs live outside the Zephyr tree), plus enabling the disabled cores in the board dts and
  raising `CONFIG_MP_MAX_NUM_CPUS`.
- The remaining `tests/drivers/uart/` suites are async-API, emul or dual-UART: **not applicable**,
  since the driver implements the interrupt-driven API and the board has one console UART.
- **SMP / multiple A55 cores.** Not supported as built: the Jailhouse cell assigns one CPU, the board
  dts disables five of six A55s, and `CONFIG_MP_MAX_NUM_CPUS=1`. The cell config lives outside the
  Zephyr tree, so this needs coordination with MediaTek's Jailhouse configs.

## Trap: an absent console is ambiguous on this platform

The first `mtk-v4.4.2` boot attempt produced no output — which is exactly what a failed GIC mapping
looks like. It was not: the host-side UART logger had been killed and nothing was capturing.

**Before treating silence as a result, confirm the logger holds the port** (`fuser /dev/ttyUSB0`).
"No console output" is both a legitimate failure signature and the signature of a dead capture, and
they are indistinguishable from the log alone.

## Trap: two ways to reset the board while working on the AFE

Both were hit here, both reset the root cell, and neither is obvious.

**Never touch Linux's AFE driver while an inmate cell holds the region.** The AFE
is granted exclusively (flags `0x93`, no ROOTSHARED), so Jailhouse unmaps it from
the root cell. A `bind`, or a runtime resume through `power/control`, then faults
in the root cell. Destroy the cell first, always.

**Never leave a wedged inmate to be destroyed.** `jailhouse cell destroy` on a
cell whose CPU is spinning in a driver poll loop takes the root cell with it. If
a run hangs, expect the next teardown to reboot the board — and read finding 9,
because that is usually why it hung.

## Does the hardware evidence still apply after the 2026-09-03 drop? Yes

The drop rewrote commits 10-19, but changed **no compiled source** — only `doc/index.rst` and two
`.webp` files. Rebuilding and comparing against the images actually booted:

```
12 bytes differ out of 57468, at offsets 52604-52615
= exactly the 12-hex-digit abbreviated SHA in the boot banner
  bfaca73809d2 -> 0d9156ec50b9
```

Every other byte is identical, so `cntfrq`, timer accuracy and boot results carry over unchanged.
Board images were still refreshed to the new shape so future reports quote a submitted-history md5.

## Open findings

| # | Finding | State |
|---|---|---|
| 1 | `MmuRegionsCheck` warns on the PINCTRL entry in `mt8188/a55/mmu_regions.c`. The checker flags every added `MT_DEVICE` entry unconditionally; a comment does not silence it. | **decided: keep the mapping**, defend upstream |
| 2 | PR A blocked on board docs: **6** `TODO(` markers (3 per board) after the 2026-09-03 drop; both board `.webp` photos now present. The remaining three per board are Jailhouse facts only MediaTek can supply. | blocking PR A |
| 3 | `mtk-v4.4.2` diverges from `mtk-genio-dev`: 4 A55 cores not 6, no Genio 510 board, no docs. | **decided: frozen**, single migration pass later |
| 4 | `verified/*` annotated tags shadow the SHA in the boot banner via `git describe`. Make them lightweight to keep both. | open, cosmetic but affects provenance |
| 5 | Both board defconfigs set `CONFIG_DCACHE_LINE_SIZE_DETECT` / `CONFIG_ICACHE_LINE_SIZE_DETECT`, which arm64 does not support — dead lines warning on every build. | **FIXED**, folded into commits 14 and 15. Verified a no-op: 0 `.config` lines differ, line size still 64, warnings 2→0 |
| 7 | **The 8 MB inmate window is real on BOTH boards.** New cells verified before transfer and installed; H8 passes with a 6 MB `.bss` array spanning `0x27a80..0x627a7c`, three times past the old 2 MB ceiling. Full suite 13/13 on each board. | **resolved** |
| 8 | **Four rpmsg cell configs were NOT updated**: `genio-{700,510}-evk-zephyr_rpmsg_{native,openamp}.cell` still grant 2 MB while the boards declare 8 MB. Running Zephyr under an rpmsg cell reacquires the latent fault. The plain and AFE cells are correct. | open, MediaTek-side |
| 9 | **The AFE cannot re-initialise after a full teardown.** The inmate following one that stopped all its streams hangs during AFE init; destroying the wedged cell resets the board. Reproducible six for six, cleared only by a reboot. Narrowed: it only affects runs that **start** a stream — `mt8188_api_reject` runs fine straight after a teardown — so the fault is in bring-up, not init. | open, PR C |
| 10 | **The AFE snippet's `pinctrl-0` does nothing under the cell.** The pin controller is granted by no cell, and the Jailhouse mediator discards ungranted writes while returning success. The eTDM pins must be muxed by the board's Linux devicetree instead. | open, needs a decision |
| 11 | **The installed AFE cell is not the one in the authoring checkout**: it grants one extra region, `0x10001400` (3 KB inside `infracfg_ao`, ROOTSHARED). Board copy md5 `1417a3b2`, checkout `3ee805c1`. A copy is staged at `tests/board/cells/` so a reflash cannot lose it. | resolved; trust the board |
| 6 | `jailhouse enable` **is required from cold on both boards** — answers doc `TODO(6)`. Also: `jailhouse cell list` exits 0 when jailhouse is disabled, so an exit-code guard silently skips the enable and fails later as `JAILHOUSE_CELL_CREATE: Invalid argument`. | resolved; both setup scripts fixed |

## Provenance of a binary

The boot banner carries the source SHA — except where an annotated tag shadows it:

```
git describe                        -> verified/2026-09-02        (no SHA)
git describe --exclude='verified/*' -> v4.4.0-13857-gbfaca73809d
```

Until `verified/*` is made lightweight, **the md5 of `zephyr.bin` is the reliable identifier**. Every
hardware report should quote it — a SHA says what should have been built, not what was.

## Images on the board, in `/root/claude_aary`

**Historical — all three shas below predate the 2026-09-10 rebases and no longer exist on
their branches.** These were placed by hand during early bring-up. The suite builds and
flashes its own images (`zephyr-g*-hwtest.bin` and friends) on every run and verifies each
by md5, so nothing below is what the tests actually exercise.

| File | Branch | Built for | md5 |
|---|---|---|---|
| `zephyr-g510.bin` | `mtk-genio-dev` `0d9156ec50b` | `mt8370_genio_510_evk` | `501853ffa9eeb7ef2721318fca01b951` |
| `zephyr-g700.bin` | `mtk-genio-dev` `0d9156ec50b` | `mt8390_genio_700_evk` | `8ab837b80144f4f943decef7d970e4e1` |
| `zephyr-g510-cntfrq.bin` | `mtk-genio-dev` `bfaca73809d` | `mt8370_genio_510_evk` | `5abb17a0f24303edd31189ea9ed124e5` |
| `zephyr-v442-g700.bin` | `mtk-v4.4.2` `c4333dd7d9c` | `mt8390_genio_700_evk` | `9c4e65f642c170d0138c46326cb14f8c` |

`README.md` and `setup-g510.sh` are alongside. The board is left running `zephyr-g510.bin`.

**The pre-existing `setup.sh` is wrong for this board**: it enables the 510 root cell but creates
`genio-700-evk-zephyr.cell` and loads the 700 image. It boots — the parts are pin-compatible — but
the banner then reports the 700 board on 510 hardware. Left unmodified; use `setup-g510.sh`.

## The test suite — `tests/`

`scripts/` has been replaced by a real suite. Run it from a checkout of the
Zephyr tree with a board attached:

```bash
./tests/run-tests.sh            # everything available
./tests/run-tests.sh --gates    # no board needed
./tests/run-tests.sh --list     # what would run
```

Everything is built from the current checkout, so a run exercises the tree
rather than whatever was last flashed. Exit status is 0 only if nothing failed.

| Tier | Tests |
|---|---|
| gates (no board) | G1 both Arm builds, G2 compliance, G3 checkpatch, G4 ADSP neutrality (`--adsp`, slow) |
| hardware | H1 boot + board string, H2 `cntfrq` + timer, H3 RX + interrupt load, H4 runtime reconfigure, H5 `uart_basic_api`, H6 `uart_interrupt_api`, H7 cell cycling, H8 memory window |
| hardware, `--gpio` | H9 GPIO and EINT, H10 upstream `gpio_basic_api`. Off by default: both need a wire between header pins 18 and 22 |

`tests/README.md` documents each test: what it runs, the expected output, and —
importantly — what it deliberately does **not** prove. Firmware sources are under
`tests/firmware/`, host drivers under `tests/host/`, board scripts under
`tests/board/`.

`detect_board` now **pushes the board-side scripts and the staged AFE cell on
every run**. They live in this repository, not on the board, and a reflash wipes
`/root/claude_aary`; because `run_cell()` sends its output to `/dev/null`, a
missing script looked exactly like a board that would not boot. `run-firmware.sh`
likewise **uploads the image it was given** rather than running whatever already
carried that name, and takes `SETUP=setup-afe.sh` to run under the AFE cell:

```bash
SETUP=setup-afe.sh tools/run-firmware.sh build/zephyr/zephyr.bin 'PASS --'
```

H8 is now wired in: the 8 MB cells landed on 2026-09-09 and it passes.
`tests/tools/verify-jailhouse-cells.py` inspects cell binaries against what the
devicetree declares, and should be run **before** cells reach a board.

Console is UART1 at 115200 on **CN3201**, FTDI adapter at `/dev/ttyUSB0`. Only one process can hold
the port (`picocom` is built with `USE_FLOCK`), so the logger and an interactive `picocom` cannot run
at once.

## Verified build gates on `mtk-genio-dev` (`0d9156ec50b`)

| Gate | Result |
|---|---|
| `git am` of the 10-patch drop onto commit 9 | clean; tree hash matched the authoring side's expected `da0392eb82e9` |
| both Arm boards + 5 ADSP targets | PASS (g700 md5 `8ab837b8…`, g510 md5 `501853ff…`) |
| ADSP behaviour neutrality | 0 config removals, loadable binaries byte-identical, all five |
| `check_compliance.py -c origin/main..HEAD` | exit 0, 1 warning (PINCTRL, accepted) |
| `LicenseAndCopyrightCheck` | PASSED (needs the CI-pinned `reuse`, force-reinstalled) |
| `checkpatch.pl -g origin/main..HEAD` | 0 errors, 0 warnings |

## Environment notes

- **The west workspace tracks one branch's manifest at a time.** Building without a matching
  `west update` produces phantom failures in unrelated modules.
- **`check_compliance.py` defaults to `HEAD~1..HEAD`** — one commit. Always pass `-c <base>..HEAD`.
- **Interactive git flags need an editor override**, they are not unavailable:
  `git commit --fixup=<sha>` then
  `GIT_SEQUENCE_EDITOR=true GIT_EDITOR=true git rebase -i --autosquash <base>`.
  This is how review fixes are folded into the commit that owns the lines.
- `reuse` must be **force-reinstalled** from the CI-pinned tarball; the fixed revision reports the
  same version string as the broken release, so plain `pip install` silently does nothing.
- Toolchain: `clang-format` 22.1.0, Zephyr SDK 1.0.1 (aarch64 + Xtensa).

## The two branches differ on purpose

| File | `mtk-genio-dev` | `mtk-v4.4.2` |
|---|---|---|
| `drivers/serial/uart_mtk_common.{c,h}` | `void uart_mtk_irq_update()` | `int`, `return 1` |
| `soc/mediatek/mt8xxx/Kconfig` | `SOC_MTK_ADSP` selects `CPU_HAS_DCACHE`, `ARCH_HAS_NOCACHE_MEMORY_SUPPORT` | neither select exists |
| `soc/mediatek/mt8xxx/Kconfig.defconfig` | `configdefault DCACHE` | `config DCACHE` / `default y` |
| `soc/mediatek/mt8xxx/mt8188/a55/mmu_regions.c` | pin controller only | pin controller **plus both GIC banks** |

The last row is now hardware-proven on both sides: `mtk-genio-dev` boots without the GIC entries
(the arch core maps them), and `mtk-v4.4.2` boots with them.

**On any base whose `arch/arm64/core/mmu.c` lacks `mmu_gic_regions`, a "redundant GIC entry" finding
is a false positive and the entries must stay.** Test the base, never the branch name:

```bash
git show <ref>:arch/arm64/core/mmu.c | grep -c mmu_gic_regions   # 0 => entries required
```

Note the check does not exist in v4.4.2's own `check_compliance.py`, so it cannot fire there today.
The rule applies at migration time.
