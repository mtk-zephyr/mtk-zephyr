# Drop 2026-09-03 (commits 10–19) — applied and verified

> ## READ THIS FIRST — your §9 is out of date
>
> Your handover lists all three hardware items as outstanding, including `[Q-1] cntfrq`. **All three
> were answered on 2026-09-02**, before this drop arrived. You wrote §9 before pulling
> `to-authoring/2026-09-02-hardware-results.md`.
>
> | Your §9 item | Status |
> |---|---|
> | `[Q-1]` `cntfrq` | **`cntfrq = 13000000`.** 13 MHz. Commit 6 needs **no** amendment. Close the question. |
> | `IRQ_TYPE_LEVEL` in commit 9 | **Confirmed on hardware.** `k_sleep(K_SECONDS(5))` = 5.004 s against a host wall clock, 0.08% error. |
> | v4.4.2 GIC MMU restore | **Confirmed on hardware.** Built from `c4333dd7d9c` and booted: `v4.4.2-18-gc4333dd7d9c2`. |
>
> Commit 9 is untouched by this drop — same object — so the timer result carries over directly.
> Full detail, including a near-miss that would have looked like the GIC fix failing, is in
> `to-authoring/2026-09-02-hardware-results.md`.

## Applied

```
git reset --hard d73f92f4a85        # commit 9, confirmed an ancestor of the branch
git am handover/0*.patch            # 10 patches, no conflicts
```

**Your tree-hash gate matches exactly:**

```
expected  da0392eb82e92a50c9cbaf38a2a4f6bfe05c459a
actual    da0392eb82e92a50c9cbaf38a2a4f6bfe05c459a
```

So the two `.webp` binary hunks survived transfer. That gate was worth having — no other check here
would have caught a corrupted binary hunk.

New tip `0d9156ec50b`, 19 commits on `origin/main` (`5a56224939a`), clean tree, all
`aary.patil@mediatek.com`. **Nothing amended.**

## Your gates

| Gate | Expected | Actual |
|---|---|---|
| `ls boards/mediatek/*/doc/img/*.webp \| wc -l` | 2 | **2** |
| `grep -rc 'TODO(' boards/mediatek/ \| grep -v ':0'` | 6 | **6** |
| `check_compliance.py -c origin/main..HEAD` | 1 warning | **exit 0, 1 warning** (PINCTRL, accepted) |
| `checkpatch.pl -g origin/main..HEAD` | clean | **0 errors, 0 warnings** |
| Longest subject | 75 | 75 |

The nine checks you cannot run all pass here, including `LicenseAndCopyrightCheck`.

## Builds

| Target | Result |
|---|---|
| `mt8390_genio_700_evk/mt8188/a55` | PASS — md5 `8ab837b80144f4f943decef7d970e4e1` |
| `mt8370_genio_510_evk/mt8188/a55` | PASS — md5 `501853ffa9eeb7ef2721318fca01b951` |
| 5 ADSP targets | PASS |

Five-way ADSP gate, in your corrected form: **zero removed, three added** on every target
(`CONFIG_SOC_FAMILY`, `CONFIG_SOC_MTK_ADSP`, `CONFIG_SOC_MT81xx_ADSP`), and the loadable binaries are
**byte-identical** to the `origin/main` builds on all five.

## The hardware evidence did NOT need re-gathering — and here is the proof

Your §4 assumed it would: *"if a board boots against the current branch, the SHA and the `zephyr.bin`
md5 in that report belong to a history we are about to rewrite."* Sound reasoning, but it does not
bite here, and I checked rather than assumed.

The tree diff against the exact commit I hardware-tested (`bfaca73809d`) is:

```
boards/mediatek/mt8370_genio_510_evk/doc/img/mt8370_genio_510_evk.webp   Bin 0 -> 59854
boards/mediatek/mt8370_genio_510_evk/doc/index.rst                       66 +++----
boards/mediatek/mt8390_genio_700_evk/doc/img/mt8390_genio_700_evk.webp   Bin 0 -> 41710
boards/mediatek/mt8390_genio_700_evk/doc/index.rst                       41 ++-----
```

Nothing outside `doc/` and `.webp` — no compiled source. Rebuilding both boards and comparing against
the images actually booted:

```
mt8390_genio_700_evk: 12 bytes differ out of 57468
mt8370_genio_510_evk: 12 bytes differ out of 57468

version string offset 52589; 'v4.4.0-13857-g' is 14 chars, so the SHA starts at 52603
differing offsets: 52604 .. 52615  = 12 bytes = exactly the 12 hex digits

  bfaca73809d2  ->  0d9156ec50b9
```

**The only difference between the tested binary and the submitted one is the abbreviated git SHA in
the boot banner.** Every other byte is identical. The `cntfrq`, timer-accuracy and boot results
therefore apply to this drop unchanged, and re-running them would exercise byte-identical code.

I have still refreshed the board images to the new shape, so any *future* hardware report quotes an
md5 belonging to the submitted history — that part of your §4 is right and worth keeping.

## Your §2a rewording is now hardware-confirmed, in both directions

Commit 10's new message says the GIC deliberately gets no entry because `arch/arm64/core/mmu.c` maps
every reg bank itself. That is now confirmed on silicon rather than by reading source:

- `mtk-genio-dev`, `mmu_regions.c` with **one** entry — boots.
- `mtk-v4.4.2`, `mmu_regions.c` with **three** — boots, and that base has no `mmu_gic_regions`.

So the paragraph is true on `main` and false on v4.4.2, exactly as you say, and both halves are now
evidenced. Your point that the message must be adapted during migration stands — it is the fourth
migration item and the easiest to forget, because nothing mechanical checks a commit message against
its own diff.

## Noted from your side

- **§6, `reuse`:** understood — released versions only, and `codeberg.org` fails TLS from your
  machine. `LicenseAndCopyrightCheck` stays mine. It passes on this drop.
- **§8, `git cherry-pick -q`:** thank you, and my replay scripts do not use `-q` — they redirect
  stdout instead, so they were not exposed to it. I do compare tree hashes rather than reading logs,
  which is the habit that saved you; this drop's gate is another instance of it.
- **§5:** agreed and closed. The `MmuRegionsCheck` rule now lives in `STATUS.md` in your formulation —
  test the base's `arch/arm64/core/mmu.c`, never the branch name.

## Board state, for whoever tests next

`/root/claude_aary` on the Genio 510:

| File | Branch | md5 |
|---|---|---|
| `zephyr-g510.bin` | `mtk-genio-dev` `0d9156ec50b` | `501853ffa9eeb7ef2721318fca01b951` |
| `zephyr-g700.bin` | `mtk-genio-dev` `0d9156ec50b` | `8ab837b80144f4f943decef7d970e4e1` |
| `zephyr-g510-cntfrq.bin` | `mtk-genio-dev` `bfaca73809d` | `5abb17a0f24303edd31189ea9ed124e5` |
| `zephyr-v442-g700.bin` | `mtk-v4.4.2` `c4333dd7d9c` | `9c4e65f642c170d0138c46326cb14f8c` |

`README.md` and `setup-g510.sh` alongside; `scripts/` on this branch has the test sample, the
timestamping host logger and the board setup script.

**The pre-existing `setup.sh` is wrong for this board** and left unmodified — it enables the 510 root
cell but creates the 700 cell and loads the 700 image. It boots, so the mismatch is invisible unless
you read the banner.

## Still outstanding

- **Hardware:** UART RX (nothing has exercised the receive path), runtime reconfigure,
  `tests/drivers/uart/*`, interrupt delivery under sustained load, deliberate cell restart, and the
  **entire Genio 700 board** — everything proven so far is 510 silicon.
- **Docs:** 6 `TODO(` remain, 3 per board. One of them — whether `jailhouse enable` is needed —
  answers itself on first bring-up, so hardware closes a doc gate too.
- **Photo licensing:** still Aary's explicit yes before the PR opens.
- **`verified/*` tags** shadow the SHA in the boot banner via `git describe`; making them lightweight
  would keep both.

**Not pushed.** Awaiting Aary's authorisation; it needs `--force-with-lease` since it rewrites 10
commits.
