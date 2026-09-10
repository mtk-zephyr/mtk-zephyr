# 2026-09-09 — PR A moved to the upstream repo; a real defect in the PR A/B split

## Summary

PR A (15 commits) now exists on `mtk-zephyr/zephyr` as `mtk-genio-mt8188`, based on
`main` (`1dbf149f7dd`, a clean sync of zephyrproject-rtos), with the `GENIO: ` prefix
stripped. Everything passes; nothing has been submitted upstream and no pull request
has been opened.

Splitting PR A out on its own surfaced a defect that the combined branch had hidden.

## The defect: PR A did not build on its own

    /cpus/cpu@400: node has a unit name, but no reg or ranges property
    /cpus/cpu@500: node has a unit name, but no reg or ranges property
    ERROR: Input tree has errors, aborting

`cpu4: cpu@400` and `cpu5: cpu@500` were added by the **PR B** commit
`dts: arm64: mediatek: add MT8188 EINT and GPIO banks`, while the board dts that
**disables** them is in the **PR A** commit `boards: mediatek: add MT8390 Genio 700
EVK`. PR A alone therefore referenced two nodes it never defined, devicetree
auto-created them without a `reg`, and dtc rejected the tree.

Counting `cpu@` nodes in `mt8188.dtsi`: 4 at the PR A tip, 6 with PR B applied.

This was invisible until now because every build we had run used the full 18-commit
branch. It also broke bisectability of the merged history: commits 13-15 did not
build until PR B landed.

Likely origin: the 2026-09-02 handover described "mt8188.dtsi now describes six A55
cores" as part of that drop, but the core addition landed in the EINT/GPIO commit
rather than in the commit that creates the dtsi.

## Fix

The two nodes moved into `dts: arm64: mediatek: add MT8188`, which is where the SoC
dtsi is created and where the full A55 cluster belongs. The EINT commit keeps only
the EINT node and the GPIO banks, which is what its subject claims.

Applied on `mtk-genio-dev` (force-pushed) so PR B is corrected too, not just the
upstream copy. Verified:

- final tree byte-identical to before the rewrite (`bd315b9ea4e`)
- all 18 commit messages, authors and trailers unchanged
- the EINT commit drops from 87 to 75 insertions and no longer mentions `cpu@`
- backup tag `pre-cpu-move` at the old tip `95a72658a6b`

## Verification of the upstream branch (`bf26ae3a4c3`)

| Check | Result |
|---|---|
| both board builds | pass |
| compliance, base `upstream-zephyr/main` | pass, warnings only |
| checkpatch | 0 errors, 0 warnings |
| hardware, Genio 510 | 9/9 including the 8 MB window |
| per-commit build sweep | 20/20, bisectable |
| 49 files added by PR A | byte-identical to the validated source |
| trailers | 15 `Signed-off-by`, 12 `Co-authored-by` |

Cherry-picking all 15 commits across 972 commits of upstream drift produced no
conflicts.

## What upstream requires before a PR merges

23 workflows trigger on `pull_request`. `ready-to-merge.yml` is a reusable workflow
that fails unless every job passed to it is `success` or `skipped`; it is called by
`doc-build.yml` and `twister.yaml`, so it is not a single global aggregator.

The check that would have caught the cpu defect is **`devicetree_checks`**.

`doc/contribute/contributor_expectations.rst` states every commit must build and pass
its tests, for bisectability — the rule the split violated. Hence the per-commit sweep
now in the suite's tooling rather than a tip-only build.

Human review, from `doc/project/release_process.rst`:

- minimum 2 approvals, one from the designated assignee
- four-eye principle at organisation level: *common/shared code* (anything not under
  `soc/`, `boards/`, `drivers/*/*`) needs an approval from outside the submitter's
  organisation; *hardware support* needs at minimum the merger to be outside it
- for PR A this bites on **`MAINTAINERS.yml`**, the one file not under
  `soc/`/`boards/`/`drivers/*/*`, which needs a non-MediaTek approval

## Test suite changes

- `run-tests.sh` gained `BASE_REF` / `--base-ref`. It hardcoded `origin/main` as the
  compliance base, which on the upstream branch would have diffed 987 commits instead
  of 15.
- `lib/common.sh` now defaults `CELL_DIR` to `$BOARD_DIR/cells`. The suite was taking
  its setup script and images from `/root/claude_aary` but its cell configs from
  `/usr/share/jailhouse/cells`, which still grant a 2 MB inmate window against the
  8 MB the board dts declares. H8 failed and read like a regression; it was correctly
  reporting the stock grant. The patched 8 MB cells sit next to the images.
- H8 polls for its verdict instead of sleeping a fixed 6 seconds, and its failure
  message now names the cell directory in use.

The 2 MB/8 MB gap in `/usr/share` needs no action here: the jailhouse change is
landing separately and the cells on the board already carry it.
