# The AFE work is on `mtk-genio-dev`, and the samples are no longer in it

Six commits, tip `fa073d3bc4f`. The temporary `mtk-genio-dev-afe` branch is
deleted. The merge was a fast-forward, so nothing already on `mtk-genio-dev` was
rewritten.

## Your three sample commits were dropped

The ten audio samples now live in `mtk-zephyr/samples` under `audio/`, a separate
repository that is also the west manifest of a T2 workspace, so one `west init -m`
brings down tree, modules and samples already matched. They are board-specific
development aids built on a driver-specific interface, which fits there and not
the Zephyr tree — and it leaves the series carrying the driver alone, 2,400 lines
lighter into review.

The sources are unchanged: eight of ten byte-identical to the originals, the
other two differing only in comment cross-references to the two that were
renamed. The `mt8188_` prefix is dropped, being redundant under `audio/` in a
MediaTek repository.

Dropping them was clean because of how you had split the series. Each of the
three was **purely** `samples/audio/` — 3, 3 and 24 files, every one — so nothing
needed separating, and nothing else in the tree referenced them. Five of the six
survivors kept their shas; the sixth was a verbatim re-parent.

## What the six are

    fa073d3bc4f  boards: mediatek: document where GPIO 38 and 40 reach the header
    2bbd535ab1b  boards: mediatek: add an AFE snippet for the Genio EVKs
    53e2a3ab253  drivers: audio: mediatek: add MT8188 Audio Front End driver
    acecc966528  drivers: clock_control: mediatek: add MT8188 topckgen driver
    0ed6164101a  drivers: clock_control: mediatek: add MT8188 apmixedsys PLL driver
    38a73170312  include: dt-bindings: mediatek: add MT8188 audio clock IDs

Verified at the tip: 12/12 per-commit builds across both boards, so the series is
bisectable; 32 compliance checks; and 9/9 on the Genio 510. The image the tree
builds is byte-identical before and after the drop, so the earlier hardware
results carry over rather than needing a repeat.

The snippet commit is the one that changed most since you sent it — it now covers
**both** EVKs rather than the 700 alone, with the eTDM pin control state moved to
`boards/mediatek/common/` and included by both boards.

## Two trailers still missing, and one of them I cannot supply

**No commit carries `Signed-off-by:`.** Only the submitter can add one.

**Four of the six carry no `Assisted-by:`** — `38a73170312`, `0ed6164101a`,
`acecc966528` and `fa073d3bc4f`. I have not touched their content, only their
parents, so I cannot honestly name a model that assisted them: that would claim
authorship of work I did not do. **You know what wrote them and I do not.** Send
the trailer text and I will add it, or say it should be dropped as a question.

`2bbd535ab1b` carries `Claude:claude-opus-5`, which is accurate — it was
substantially rewritten here. `53e2a3ab253` has four fixes of mine folded in and
still carries none; that one I could supply, and have not, so that the four
undecided ones are not settled by precedent.

Both trailers mean rewriting six commits on `mtk-genio-dev`, which is
force-pushed by policy, so it is cheap — but it is no longer free, because the
samples repository's `west.yml` tracks that branch and anyone on it has to
re-sync. Worth doing in one pass rather than two.

## Still open, and only one is not bookkeeping

- **The AFE cannot re-initialise after a full teardown.** The inmate following
  one that stopped all its streams hangs during bring-up, and destroying the
  wedged cell resets the board. Narrowed since the last note: it only affects
  runs that **start** a stream, so the fault is in bring-up, not init.
- **The GPL-2.0 header on `mt8188_afe_reg.h`** — a MediaTek licensing decision.
- **The snippet's `pinctrl-0` applies nothing under the hypervisor.** The pin
  controller is granted by no cell and the mediator discards ungranted writes
  while returning success, so the eTDM pins have to be muxed by the board's Linux
  devicetree. The state is still worth shipping — it is correct, and it would
  work on a cell that granted pinctrl — but a reviewer will ask.
