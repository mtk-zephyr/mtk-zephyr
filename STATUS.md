# STATUS — as of 2026-09-03 (drop pushed; both boards tested)

Current state of the MediaTek Genio work on `github.com/mtk-zephyr/mtk-zephyr`. This file is
overwritten on every update; `git log` on this branch is the history.

## Branch tips

| Branch | Tip | Contents | Verified |
|---|---|---|---|
| `main` | `5a56224939a` | upstream Zephyr mirror, no MediaTek work | n/a |
| `mtk-genio-dev` | `0d9156ec50b` | 19 commits (drop 2026-09-03) | builds, full compliance, **and hardware on both boards** |
| `mtk-v4.4.2` | `c4333dd7d9c` | 18 commits on the `v4.4.2` release tag | builds, compliance, **boots on hardware** |

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
| **`mtk-v4.4.2` image boots** | **PASS, natively** | PASS (cross-board) |

`SYS_CLOCK_HW_CYCLES_PER_SEC = 13000000` confirmed on both parts — **no amendment needed**. The
`IRQ_TYPE_LEVEL` arch-timer fix and the v4.4.2 GIC MMU restore are both confirmed, the latter now on
the hardware it was built for.

Timer accuracy was measured against the **host**, not `k_uptime_get()` — the latter derives from the
timer under test and cannot detect a misconfigured one.

## UART and interrupt tests — all pass on the Genio 700

| Test | Result |
|---|---|
| UART RX echo, 64 / 2000 / 8000 bytes | **PASS**, byte-for-byte identical |
| Interrupt load | **PASS** — `rx=10065 isr=10065`, zero bytes lost |
| Runtime reconfigure | **PASS**, verified on the wire incl. a negative control |
| `tests/drivers/uart/uart_basic_api` | **PASS 7/7** |
| `tests/drivers/uart/uart_interrupt_api` | **PASS 1/1** |
| Cell shutdown/restart cycling | **PASS**, 6/6 |

`isr == rx` exactly: one interrupt per byte, RX FIFO trigger effectively 1. Not a bug; it does mean
RX throughput is interrupt-bound (6.5 KiB/s of an 11.5 KiB/s line rate).

The remaining `tests/drivers/uart/` suites are async-API, emul or dual-UART — the driver implements
the interrupt-driven API and the board has one console UART, so they are **not applicable** rather
than untested.

## Still not tested on hardware

- **The Genio 510 has not run the new tests** (RX, reconfigure, in-tree suites, load, cycling). It
  passed everything that existed when it was on the bench. Images and scripts are branch-independent,
  so re-running there is short — worth doing before the customer drop, since the 510 is the part that
  ships.
- **SMP / multiple A55 cores.** Not supported as built: the Jailhouse cell assigns one CPU, the board
  dts disables five of six A55s, and `CONFIG_MP_MAX_NUM_CPUS=1`. The cell config lives outside the
  Zephyr tree, so this needs coordination with MediaTek's Jailhouse configs.

## Trap: an absent console is ambiguous on this platform

The first `mtk-v4.4.2` boot attempt produced no output — which is exactly what a failed GIC mapping
looks like. It was not: the host-side UART logger had been killed and nothing was capturing.

**Before treating silence as a result, confirm the logger holds the port** (`fuser /dev/ttyUSB0`).
"No console output" is both a legitimate failure signature and the signature of a dead capture, and
they are indistinguishable from the log alone.

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
| 5 | **Both board defconfigs set `CONFIG_DCACHE_LINE_SIZE_DETECT=y` and `CONFIG_ICACHE_LINE_SIZE_DETECT=y` (lines 8-9), which arm64 does not support.** Both are forced to `n` and emit a Kconfig warning on **every** build. Values land on the correct static default of 64 for Cortex-A55, so nothing is broken — but the lines are dead and upstream review will flag them. Fix: delete both from both defconfigs; keep `CONFIG_CACHE_MANAGEMENT=y`. | open, needs a fold into commits 14 and 15 |

## Provenance of a binary

The boot banner carries the source SHA — except where an annotated tag shadows it:

```
git describe                        -> verified/2026-09-02        (no SHA)
git describe --exclude='verified/*' -> v4.4.0-13857-gbfaca73809d
```

Until `verified/*` is made lightweight, **the md5 of `zephyr.bin` is the reliable identifier**. Every
hardware report should quote it — a SHA says what should have been built, not what was.

## Images on the board, in `/root/claude_aary`

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

## Reproducing the hardware tests — `scripts/`

| File | Purpose |
|---|---|
| `hwtest-main.c` | drop-in `samples/hello_world/src/main.c`: prints `cntfrq`, then three `SLEEP_START`/`SLEEP_END` pairs |
| `uartlog.py` | host-side logger, timestamps each line on arrival — the external clock the sleep test needs |
| `setup-g510.sh` | board-side: tear down, create the 510 cell, load at `0x8000`, start |

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
- **Interactive git flags are unavailable** — no `git rebase -i`, including `--autosquash`.
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
