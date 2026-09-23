# The re-initialisation hang was route(), and it was never about re-initialisation

Fixed on `mtk-genio-dev` at `6cec5371c56`. One commit, seventeen lines, all in
`mt8188_afe_api_route()`.

## What it actually is

`mt8188_afe_set_route()` writes the AFE_CONN interconnect matrix. That matrix
sits in the **a1sys timing domain**. `route()` runs before `start()`, so nothing
is holding that domain up — and reaching those registers with it down **stalls
the bus and never returns**. No error, no timeout: the caller simply stops, mid
call, with the cell still marked running and its CPU spinning.

It only ever worked because the domain happened to be up. On a fresh boot Linux
leaves it on; within a session a previous stream leaves it on. The first
`stop()` that released it — the reference count reaching zero, working exactly
as designed — turned every later `route()` into a hang, in a freshly loaded
image too, until the board was rebooted.

So the defect was never in initialisation. It is an **ordering dependency in the
public API** that was invisible while something else kept the clock alive. The
documented call order, configure → route → set_buf → start, was only ever safe
by luck.

## The fix

`route()` takes the a1sys domain around its writes and releases it after. The
rate passed selects the domain rather than a stream: any 48 kHz-family rate maps
to APLL1/a1sys, which is where the matrix is, whatever rate the stream being
routed will eventually run at. The existing reference count makes it safe
against a stream already holding the same domain, and releasing afterwards means
nothing is left powered that was not before.

## How it was found, and what did not work

A probe that announced each call before making it put the hang in `route()`
rather than `start()`, which is where the earlier symptom description had
pointed. Reversing the order — `start()` before `route()` — made it go away,
which named the timing domain as the missing piece.

Four narrower candidates were tried on hardware and **all four hung**: enabling
the topckgen `CLK_TOP_A1SYS_HP` gate at init; enabling the audsys a1sys gate
plus `A1SYS_TIMING_ON` at init; forcing the `A1SYS_HP` mux back to clk26m and
enabling its gate; and the APLL1 PLL on its own. Taking the whole domain worked,
which is what the fix does at the one call that needs it.

Worth recording because it is counter-intuitive: reverting the mux to clk26m and
clocking a1sys from it is **not** enough to reach those registers, even though
clk26m is live. Whatever the matrix needs, it is not merely a running clock.

## A trap in measuring this

Any run that hangs leaves a wedged inmate, and destroying that inmate resets the
board — which restores the good state. So the run *after* a hang is testing a
freshly booted board, and a broken candidate looks fixed. One false positive was
recorded that way before the trial harness started asserting that uptime rises
monotonically across the arming run and the candidate run, and refusing to
report a result otherwise.

## Verified

- The image that hung on its second run now completes three times consecutively,
  uptime monotonic throughout.
- The sequence that used to wedge the board — C8, which stops every stream, then
  a loopback — passes end to end, followed by C9.
- `loopback_dl11_ul8` is unchanged: `rot 0`, constant lag, every frame verified.
- Suite 9/9 on the Genio 510; 33 compliance checks, the only failure being the
  missing sign-off.

## Still open on PR C

The **GPL-2.0 header on `mt8188_afe_reg.h`**, and the **snippet's `pinctrl-0`**,
which applies nothing under the hypervisor. Both need a decision rather than
code. With this fixed, they are the whole list.
