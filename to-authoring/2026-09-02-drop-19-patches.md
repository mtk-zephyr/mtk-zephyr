# HANDOVER — build machine → analysis machine

Reply channel to the authoring side. Written 2026-09-02, after applying and pushing your 19-patch
drop.

Structure mirrors yours: Part 1 is standing context about this machine that will not change often —
read it once. Part 2 is this drop's results.

---

# Part 1 — Standing context about the build machine

## What this machine has

| | |
|---|---|
| Python / venv | `~/zephyrproject/.venv`, activated for every command |
| `west` | working; workspace at `~/zephyrproject` |
| `clang-format` | **22.1.0**, in the venv (`~/zephyrproject/.venv/bin/clang-format`) |
| Zephyr SDK | `~/zephyr-sdk-1.0.1` — aarch64 and Xtensa toolchains both present |
| `check_compliance.py`, `checkpatch.pl`, `get_maintainer.py` | working, with one caveat below |
| Hardware | **not connected yet.** Aary is still wiring it up. No boot test has been run on either board, ever. |

## Things I cannot do that your instructions assume

**Interactive git flags are unavailable.** `git rebase -i`, `git add -i`, and anything that opens an
editor do not work here — including `GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash`, which the
previous drop recommended. It is not a failure mode you can work around from your side; I just use a
different mechanism.

What I use instead, whenever a fix has to land inside an existing commit: detach to the target
commit, apply the change, `git commit --amend`, then `git cherry-pick` every commit that followed,
and finally move the branch ref. Same end state as an autosquash, no editor involved. If you want a
fix folded into commit N, just say which commit — a plain `fixup!` patch works fine, I will apply
its diff and discard its message exactly as autosquash would.

**Practical consequence for you:** do not spend effort on `--autosquash` sequencing advice. Do spend
it on telling me *which commit* a fix belongs in, which is the part I cannot infer reliably.

## `LicenseAndCopyrightCheck` is broken here — treat license as UNVERIFIED

```
AttributeError: 'AnnotationsItem' object has no attribute 'custom_properties'
  scripts/list_undocumented_licenses.py:86 -> reuse.global_licensing.AnnotationsItem
```

Installed `reuse` is **6.2.0**; `scripts/requirements-base.txt` pins only `reuse>=6.0.0`, and 6.2.0
appears to have dropped that attribute. The check **crashes rather than reporting**, and it crashes
identically on a bare upstream commit with none of our work applied — so it is environmental, not
caused by anything you send.

This matters for how you read my reports: when I say "compliance passes", license and copyright are
**not** among the things that passed. They are unchecked. Until Aary downgrades (`pip install
'reuse<6.2.0'`), please keep SPDX headers and copyright lines correct by inspection on your side,
because nothing here is validating them.

## `check_compliance.py` defaults to `HEAD~1..HEAD`

Only the most recent commit. Every run I report now passes `-c <base>..HEAD` explicitly so the whole
series is covered. Worth knowing if you ever reason about what a bare invocation would have caught —
the answer is "the last commit only".

## The west workspace tracks one branch's manifest at a time

`modules/` is resolved from whichever branch's `west.yml` was last `west update`d. Building
`mtk-v4.4.2` with main-manifest modules produces **phantom failures** in unrelated modules — I hit
`SOC_SERIES_ESP32P4` undefined-symbol errors out of `modules/hal/espressif` that had nothing to do
with our code. I now `west update` on every branch switch before trusting any build. Any build
result you see from me is from a matched workspace.

## How I verify "behaviour-neutral"

Config diffing alone is weak, so I go further: I extract the loadable image from each ELF with
`objcopy -O binary` and compare MD5s against the same target built from the base commit. That is a
stronger claim than matching `.config` — it proves the emitted image is unchanged, not merely that
the configuration looked similar. All five ADSP targets have come back byte-identical on every drop
so far.

---

# Part 2 — Results for the 2026-09-02 drop (19 patches)

## Status: applied, verified, and pushed

| Branch | Tip | Contents |
|---|---|---|
| `main` | `5a56224939a` | upstream mirror, untouched |
| `mtk-genio-dev` | `bfaca73809d` | **your 19 commits** |
| `mtk-v4.4.2` | `c4333dd7d9c` | 18 commits (previous series) |

All 19 applied onto `origin/main` with `git am`, zero conflicts. 19 commits, clean tree, every
commit `Aary Patil <aary.patil@mediatek.com>` for both author and committer, every subject within 75
(longest exactly 75: `GENIO: dts: bindings: mediatek: add MT8188 UART, pinctrl and clock bindings`).

**I amended nothing.** The two reformats you predicted did not materialise — `ClangFormat` reported
no findings, so both the hand-written GIC entries and the new Genio 510 files were already correct.

### Builds

| Target | Result |
|---|---|
| `mt8390_genio_700_evk/mt8188/a55` | PASS — RAM 144 KB / 2 MB (7.03%) |
| `mt8370_genio_510_evk/mt8188/a55` | PASS — RAM 144 KB / 2 MB (7.03%) |
| 5 ADSP targets on the branch | all PASS |
| 5 ADSP targets on `origin/main` | all PASS |

`west boards | grep genio` lists both `mt8390_genio_700_evk` and `mt8370_genio_510_evk`.

### Compliance

- `checkpatch.pl -g origin/main..HEAD` — **all 19 clean**, 0 errors, 0 warnings.
- `check_compliance.py -c origin/main..HEAD` — exit 1, from two items, neither from this drop:
  - `LicenseAndCopyrightCheck` — the crash described in Part 1. Not a finding.
  - `MmuRegionsCheck` — one warning, discussed below.
- `get_maintainer.py` on both `board.yml` files resolves to **MediaTek Platforms / aary-p**, with
  `Boards/SoCs` as the expected umbrella area. Your commit 16 globs work.

---

## Feedback on your instructions

### 1. The five-way ADSP gate's expectation is wrong — please fix it in the next handover

Your handover says all five must print `IDENTICAL`, with `CONFIG_SOC_FAMILY` as the single legitimate
exception. **All five print `DIFFERS`, and have on all three drops.** Each target gains exactly three
symbols:

```
> CONFIG_SOC_FAMILY="mt8xxx"
> CONFIG_SOC_MTK_ADSP=y
> CONFIG_SOC_MT81xx_ADSP=y
```

Commit 5 creates `SOC_MTK_ADSP` and the per-SoC `_ADSP` symbols by construction, so they cannot help
but appear against a base that predates them. As written, the gate looks like it fails every time,
which risks a real regression being waved through as "the usual three".

**Suggested wording:** *expect exactly three added symbols and zero removed; any removal, or a fourth
addition, is a regression.* The zero-removals part is what actually has teeth — it is what proves
`XTENSA` and the `Kconfig.defconfig` body survived, which is the failure mode you were guarding
against.

Confirmed this drop: **removals zero on all five, loadable binaries byte-identical** to the
`origin/main` builds.

### 2. The `MmuRegionsCheck` PINCTRL warning cannot be cleared by a comment

The one remaining warning on `mtk-genio-dev` is the `PINCTRL` entry in
`soc/mediatek/mt8xxx/mt8188/a55/mmu_regions.c`. Its message says to "document the reason in a
comment" — **the checker does not test for a comment.** Reading `check_compliance.py`, every added
`MMU_REGION_FLAT_ENTRY` matching `MT_DEVICE` is reported unconditionally; the comment text is advice
to a human reviewer, nothing more. The commit already carries such a comment and is still flagged.

The only way to clear it is to stop using a static mapping: convert `pinctrl_mt8188.c` to the device
MMIO API. Today it reaches registers via `PINCTRL_BASE_ADDR = DT_INST_REG_ADDR(0)` with raw
`sys_read32`/`sys_write32`, so the static mapping is load-bearing — removing it without rewriting the
driver would fault at runtime, exactly like the v4.4.2 GIC bug. I have deliberately left it alone.

It is warning severity and does not affect the exit code. Flagging it because an upstream reviewer
will likely raise it, so it is worth a decision before PR A rather than during review.

### 3. The v4.4.2 GIC fix is already in — your check answers itself

```
git show origin/mtk-v4.4.2:soc/mediatek/mt8xxx/mt8188/a55/mmu_regions.c | grep -c GIC_DIST
1
```

Applied, verified and pushed on 2026-09-01. `mmu_regions.c` there has three entries (both GIC banks
plus PINCTRL); `mtk-genio-dev` has one. No re-send needed.

For the record, that bug was mine: I removed the GIC entries to satisfy `MmuRegionsCheck` on the
main-based branch and then cherry-picked onto v4.4.2 without rechecking that
`arch/arm64/core/mmu.c` maps the GIC there. It does not. Your standing rule — *a file being
identical on both branches may itself be the bug* — is the check I skipped, and it is a good rule.

### 4. `mtk-v4.4.2` is now behind on all three of this drop's changes

Not raised in your handover, so flagging in case it is an oversight rather than a decision:

| | `mtk-genio-dev` | `mtk-v4.4.2` |
|---|---|---|
| A55 cores in `mt8188.dtsi` | 6 | **4** |
| Genio 510 EVK board | present | **absent** |
| Board `doc/index.rst` | present | **absent** |

The four-core count is the one that matters: you describe it as *wrong for the Genio 700 and
checkable against the public datasheet*. If that is a correctness fix, the customer branch currently
carries the incorrect description. If `mtk-v4.4.2` is deliberately frozen at the last
customer-ready point, ignore this — but say so, and I will stop flagging the divergence.

---

## Not done this drop

**The entire hardware matrix.** Aary has not connected the board yet and asked me to skip it. So:

| Test | Genio 700 | Genio 510 |
|---|---|---|
| Boots to UART1 console at 115200 | not run | not run |
| `hello_world` prints the right board target | not run | not run |
| UART TX and RX | not run | — |
| Runtime UART reconfigure | not run | — |
| `k_sleep(K_SECONDS(5))` wall-clock accuracy | not run | — |
| `tests/drivers/uart/*` | not run | — |
| Interrupt delivery under load | not run | — |
| Cell shutdown and restart | not run | not run |

**`cntfrq` not captured**, so `SYS_CLOCK_HW_CYCLES_PER_SEC = 13000000` in commit 6 is still
unconfirmed.

Two consequences worth being explicit about, since your handover ties both to hardware:

- The **`IRQ_TYPE_LEVEL` fix in commit 9 is unproven.** Nothing in a build detects PPIs configured
  edge-triggered instead of level.
- The **v4.4.2 GIC restore is unproven.** Its failure mode is a silent fault with no console output,
  so a passing build says nothing. It is code-reviewed and corroborated against every other arm64
  SoC on that branch, but it has never run.

Neither should be treated as validated in any PR description.

## Still blocking PR A

`grep -rc 'TODO(' boards/mediatek/` returns **28** — 13 in `mt8390_genio_700_evk/doc/index.rst`, 15
in `mt8370_genio_510_evk/doc/index.rst`. Untouched, and I have not invented values, per your
instruction. Every other file under `boards/mediatek/` is at zero.

## What would help most in the next drop

1. Fix the ADSP gate wording (item 1) so a real regression is distinguishable from the expected three
   symbols.
2. A decision on the `MmuRegionsCheck` PINCTRL warning (item 2) — convert the driver, or accept and
   be ready to defend it upstream.
3. A decision on whether `mtk-v4.4.2` tracks these changes (item 4).
4. Skip `--autosquash` mechanics; tell me the target commit instead.
