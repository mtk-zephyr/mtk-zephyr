# mtk-genio-dev migrated to PR A review round 2

`mtk-genio-dev` is at `a3a352da5ce`, 22 commits on `origin/main`: the 16 round-2
PR A commits plus the 6 PR B commits.

The PR A portion is **commit-for-commit identical to what is under review**. Of
the 16, fifteen have byte-identical diffs; the sixteenth differs only in hunk
offset (`@@ -4025` against `@@ -4026`) because upstream touched `MAINTAINERS.yml`
in the 97 commits between the PR base and ours — the block we add is the same
text. All sixteen messages and authors match, modulo the `GENIO: ` prefix, and
all 58 files the series adds are identical blob for blob.

`pra-r2` tags the PR A tip, so PR B is addressable as `pra-r2..mtk-genio-dev`.

## What round 2 changed, and why it needed a hardware run

Two commits differ from round 1: a new one moving the shared audio DSP sources
into `soc/mediatek/mt8xxx/common/adsp/`, and a rework gating both DSP drivers on
devicetree rather than a Kconfig symbol. Fourteen subjects are unchanged.

That is a structural change — 26 files across all five DSP SoCs, including linker
scripts — so it is exactly the kind that a clean rebase cannot vouch for. It is
behaviour-neutral in the strong sense: **byte-identical loadable images on all
five DSP targets**, and the Arm side boots, muxes, clocks and takes interrupts
unchanged.

Worth recording: round 2 adds one Kconfig symbol *fewer* than round 1 for the
same result. Round 1 introduced `MTK_ADSP`; round 2 drops it and keys off
`DT_HAS_*` instead. The neutrality gate could not see that on its own — it
compares base against the branch, and `MTK_ADSP` never existed in the base — so
the absence shows up only when the two rounds are compared directly.

## Verification at `a3a352da5ce`

| | |
|---|---|
| gates | pass — checkpatch 0 errors 0 warnings |
| compliance | pass, warnings only |
| ADSP neutrality, five targets | byte-identical loadables |
| per-commit sweep | 39/39, bisectable |
| Genio 700 | **11/11** |
| Genio 510 | **11/11** |

The compliance warnings are ClangFormat on `common/adsp/{ipi,irq,mbox,soc}.c`
and `soc.h` — pre-existing ChromiumOS-authored files that round 2 relocates.
ClangFormat treats a moved file's lines as added and flags formatting that was
already there. Not ours to reformat, and the check is advisory: upstream's
compliance workflow warn-lists it.

## The resync worked the way the policy says it should

All six PR B commits came through with **identical diffs and identical
messages** — only their parents changed. Nothing was rewritten, re-normalised or
re-decided. That is the distinction the branch policy now turns on:
re-parenting is cheap and safe, rewriting is not. The next resync should look
the same:

    git rebase --onto pra-r3 pra-r2 mtk-genio-dev

Two things that made it cheap and are worth repeating:

- **The impact check came first.** Before migrating, the question was whether any
  file PR B *modifies* differed between rounds. Thirteen such files, none
  changed. That made the migration tidiness rather than a fix, and predicted the
  zero conflicts that followed.
- **A trial run preceded the real one.** Cherry-picking both halves onto a
  scratch branch, then building, cost a few minutes and turned "how much effort
  is this?" into a measured answer instead of an estimate.

## Suite changes

`tools/run-firmware.sh` and H10's result counter were both corrected. H10 had
been reporting `0 passed, 0 skipped` while still passing, because the UART log
prefixes every line with a timestamp and the `^`-anchored pattern never matched.
It now counts from the TESTSUITE SUMMARY block, and — more importantly — a run
that reports success having executed **zero** tests is now a failure, naming the
likely cause (a missing board overlay). A test that can report "0 passed" as a
pass is worse than one that simply miscounts.

Past H9 and H10 verdicts stand; only the H10 detail string was ever wrong.

## Still open

- **The dropped `*_mtk_common.c` split.** Unchanged and still worth settling
  before PR C fixes a precedent. PR D (MT8365) is where a shared layer would earn
  its place back.
- **Whether the rewritten GPIO and EINT drivers should credit the authors of the
  MediaTek originals.**
- `mtk-v4.4.2` is untouched at `ee452133d05` and now **two review rounds
  behind**. Under the branch policy that is the correct state: it takes a feature
  once the feature is structurally settled, not on a schedule. Had round 1 been
  ported last week, it would already owe a fix-on-top commit undoing a file
  layout that existed for a few days.
- Four rpmsg inmate cells still grant 2 MB.
