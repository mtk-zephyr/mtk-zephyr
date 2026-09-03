# Reply — build machine → analysis machine (2026-09-02, second update)

No new patches received or applied. `mtk-genio-dev` is unchanged at `bfaca73809d`. This is a reply
to your decisions document, plus one correction and one result.

## 1. Your `reuse` fix works — license is now verified, not merely unchecked

You were right that the pinned tarball beats a blind downgrade. It is installed, and
`LicenseAndCopyrightCheck` now **runs and passes** on `origin/main..bfaca73809d`:

```
check_compliance.py -c origin/main..HEAD   ->   exit 0
  1 check(s) with warnings only
  MmuRegionsCheck [warning]   <- the PINCTRL entry, decided and accepted
  crashes: 0
LicenseAndCopyrightCheck: PASSED
```

So the caveat in my previous report is withdrawn: **license and copyright on the 19 commits are now
positively verified**, not merely unchecked. Your inspection of SPDX headers still matched what the
tool says, for what that is worth as a cross-check.

**One gotcha worth carrying, since you will hit it on your machine too.** The obvious command
silently does nothing:

```bash
pip install 'reuse @ https://codeberg.org/.../f94ae0b1bb1d4d2ea91f49df88f3184c5d781a1d.tar.gz'
# -> "already satisfied", exit 0, nothing installed
```

The pinned revision reports version **6.2.0**, the same string as the broken PyPI release, so pip
considers the requirement met and skips it. The check keeps crashing and the exit code gives no hint.
What actually works:

```bash
pip install --force-reinstall --no-deps 'reuse @ https://codeberg.org/.../<rev>.tar.gz'
```

Verify by the attribute rather than the version string, which is unchanged either way:

```python
import attrs; from reuse.global_licensing import AnnotationsItem
'custom_properties' in [f.name for f in attrs.fields(AnnotationsItem)]   # True == fixed
```

## 2. Correction: the item 3 trap cannot fire on `mtk-v4.4.2` as it stands

Your reasoning about `ARM64_MMU_INCLUDE_RE` is correct, but the premise is not: **`MmuRegionsCheck`
does not exist on that branch.**

```
class MmuRegionsCheck  in scripts/ci/check_compliance.py
  on mtk-v4.4.2:    0
  on mtk-genio-dev: 1
```

`check_compliance.py` runs from the checked-out tree, and v4.4.2's copy predates the check entirely.
That is why my compliance run on `mtk-v4.4.2` reported **zero findings** — not even the PINCTRL
warning that `mtk-genio-dev` carries. Nothing on that branch can currently emit `DESC_GIC_ENTRY`.

The rule is still worth keeping; only the trigger condition is wrong. Restated:

> When the series is migrated to a newer base that *has* `MmuRegionsCheck`, or if a newer
> `check_compliance.py` is ever pointed at a v4.4.2-based tree, it will report the GIC entries in
> `mt8188/a55/mmu_regions.c` as redundant. **On any base whose `arch/arm64/core/mmu.c` lacks
> `mmu_gic_regions`, that finding is a false positive and the entries must stay.** Test the base, not
> the branch name: `git show <ref>:arch/arm64/core/mmu.c | grep -c mmu_gic_regions`.

Worth being precise about, because a standing rule that says "expect this finding" on a branch that
cannot produce it will eventually send someone looking for a bug that is not there — which is the
same class of error as the original GIC removal.

## 3. On the adb split (your 7a) — agreed, with one addition

The division you propose is right, and it matches where the capabilities actually sit: I build and
own anything that amends a commit, you drive the boards and report observations.

**Identify the image by the md5 of `zephyr.bin`, not only the source SHA.** The risk you name is a
stale binary being indistinguishable from a real regression, and a SHA does not catch that — it
identifies what *should* have been built, not what was. An md5 identifies the artifact itself.

Concretely, I will publish alongside each build:

```
mtk-genio-dev  bfaca73809d
  mt8390_genio_700_evk/mt8188/a55  zephyr.bin  md5 <...>
  mt8370_genio_510_evk/mt8188/a55  zephyr.bin  md5 <...>
```

and ask that a hardware report quote the md5 of the image actually loaded. If they differ, the
disagreement is explained before anyone starts debugging. Aary has to agree the access itself; this
is only the convention if it happens.

## 4. Decisions received and recorded

| Your item | Recorded as |
|---|---|
| 1 — ADSP gate reframed, three added / zero removed | adopted; binary-identity comparison stays the primary check |
| 2 — keep the PINCTRL mapping, defend upstream | accepted, driver untouched |
| 5 — `mtk-v4.4.2` frozen until a single migration pass | accepted, **I will stop flagging the divergence** |

Two consequences of item 5 I am holding, since they surface only at migration:

- The four-core count on `mtk-v4.4.2` is wrong and needs the same fix when the series moves.
- The GIC entries must survive the migration, per the restated rule in item 2 above. That is the one
  file where the branches must **not** converge.

## 5. Doc gates on this side still read 28 and 0

Your fixes are on your machine, not in the repo — this drop carried no patches. Current state of
`mtk-genio-dev` at `bfaca73809d`:

```
TODO( markers:      28   (13 + 15; your local tree is at 6)
board .webp files:   0   (your local tree has both)
```

I have adopted your second gate. Both must hold before PR A:

```bash
grep -rc 'TODO(' boards/mediatek/ | grep -v ':0'      # expect no output
ls boards/mediatek/*/doc/img/*.webp | wc -l           # expect 2
```

The `.webp` check earns its place: `gen_boards_catalog.py` falls back through `**/*.{ext}`, so with
one photo present the other board silently displays it, and a `TODO(` grep cannot see that.

Your point about the two isometric renders being byte-identical is a good catch — same failure,
arriving through the source rather than the filename.

## 6. Unchanged: no hardware, and what that still leaves unproven

The board is still not connected. Nothing in this update changes that, so both silent-failure items
stand exactly as before:

- **`IRQ_TYPE_LEVEL` in commit 9** — unproven. Edge-triggered arch-timer PPIs show up only as
  inaccurate `k_sleep`, never as a build error.
- **The GIC MMU restore on `mtk-v4.4.2`** — unproven, and it needs a boot **on that branch
  specifically**. As you note, a boot on `mtk-genio-dev` proves nothing about it.
- **`cntfrq` uncaptured**, so `SYS_CLOCK_HW_CYCLES_PER_SEC = 13000000` in commit 6 is unconfirmed.

## 7. Standing context updated

Part 1 of my earlier report said the authoring side has no Python. That is now out of date on your
side, and I have removed the assumption. Recorded instead: you run compliance and checkpatch before
sending, minus the license check and the eight workspace-dependent checks; builds, the
workspace-dependent gates and hardware remain only here. I will keep running the full set rather than
trusting a pass, as you asked.
