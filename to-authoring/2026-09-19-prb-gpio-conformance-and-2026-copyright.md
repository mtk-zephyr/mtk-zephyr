# PR B passes the upstream GPIO conformance suite; copyright settled at 2026

`mtk-genio-dev` is at `8b348fc37a2`, 21 commits on `origin/main`: 15 PR A review
round 1 plus 6 PR B.

## tests/drivers/gpio/gpio_basic_api — both boards

`PROJECT EXECUTION SUCCESSFUL` on the Genio 700 and the Genio 510, identically.

| suite | result |
|---|---|
| `gpio_port` | 1/1 pass |
| `gpio_port_cb_mgmt` | 3/3 pass — add/remove, enable/disable, **self-remove** |
| `gpio_port_cb_vari` | 1/1 pass |
| `after_flash_gpio_config_trigger` | 2 skipped, see below |

**The two skips are the test opting itself out, not a gap.** It prints
`Open drain not supported.` and calls `ztest_test_skip()` when a driver returns
`-ENOTSUP` for `GPIO_OPEN_DRAIN`, which this driver does deliberately: open-drain
lives in the pin controller, not the GPIO block. The pull-up path behaves the same
way — `NOTE: pull-up not supported; trying as output high` — and the suite carries
on. Both are documented fallbacks in the test source, not workarounds.

This was worth running. It covers what the bespoke H9 tests deliberately do not:
callback add/remove, enable/disable, and removing a callback from inside the
callback. That last one is reentrancy a driver can get quietly wrong, and no
amount of our own testing would have looked at it.

Two overlays now ship with the series, following the convention a dozen other
boards already use:

    tests/drivers/gpio/gpio_basic_api/boards/mt8390_genio_700_evk_mt8188_a55.overlay
    tests/drivers/gpio/gpio_basic_api/boards/mt8370_genio_510_evk_mt8188_a55.overlay

Four lines each, naming bank 1 pins 6 and 8. They carry no CI cost: the suite is
filtered on `dt_compat_enabled("test-gpio-basic-api")` and gated behind a
`gpio_loopback` fixture, so it only runs on a bench with the wire fitted. Anyone
with a jumper between GPIO 38 and 40 can now reproduce us.

## Copyright: 2026, decided

The inconsistency the drop raised is settled. Every MediaTek file PR B adds now
reads `Copyright (c) 2026 MediaTek Inc.`, matching PR A. Normalised inside the
commit that adds each file, so a header is correct at birth rather than appearing
as a later edit.

Seven files changed, and the whole diff against the previous tip is those seven
header lines plus the two new overlays — nothing else moved.

**Files held by other parties were not touched.** Several under `soc/mediatek`
carry `Copyright 2024 The ChromiumOS Authors`; rewriting another party's notice is
not ours to do, and Apache-2.0 section 4(c) requires retaining it. The normaliser
skips them by name, and zero were modified.

## Verification at `8b348fc37a2`

| | |
|---|---|
| gates | pass — compliance clean, checkpatch 0 errors 0 warnings |
| per-commit builds | 18/18, bisectable |
| H9, driver tests | 19/19 on both boards |
| H10, `gpio_basic_api` | pass on both boards |
| sign-offs | 21/21, one per commit |

The suite runs both GPIO tiers under `--gpio`; they need the jumper, so they are
off by default.

## Still open, and worth settling before PR C takes shape

- **The dropped `*_mtk_common.c` split.** The drop called this the decision most
  worth overruling: it makes the upstream drivers diverge from the internal branch
  and costs MediaTek on every future sync. It matters more now than it did — PR D
  (MT8365) is where a shared layer would earn its place back, and re-splitting
  later is mechanical while the reverse is not. Whatever PR C establishes about
  structure will be the precedent.
- **Whether the rewritten drivers should credit the authors of the MediaTek
  originals.** They were rewritten rather than ported, because the vendor versions
  cannot compile against the landed bindings, so no `Co-authored-by` was added.
  Still Aary's call and his trailer to add.
- Four rpmsg inmate cells still grant 2 MB. `genio-*-evk-zephyr` and
  `-zephyr-afe` are now 8 MB on the reflashed boards; `-zephyr_rpmsg` is not.
