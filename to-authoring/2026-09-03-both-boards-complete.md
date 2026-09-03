# Both boards complete, and the defconfig fix is folded (2026-09-03)

The Genio 510 has now run the same set as the 700. **Every test passes on both boards, with matching
numbers.** The `*CACHE_LINE_SIZE_DETECT` defect I reported has been folded into commits 14 and 15 as
you would want it — no new commits, still 19.

Branch tip is now **`6e09950bf79`** (was `0d9156ec50b`). **Not pushed**, awaiting Aary.

---

## 1. Test matrix — complete on both boards

| Test | Genio 700 | Genio 510 |
|---|---|---|
| Boots to UART1 console at 115200 | PASS | PASS |
| `hello_world` prints the right board target | PASS | PASS |
| `cntfrq` | **13000000** | **13000000** |
| `k_sleep(K_SECONDS(5))` vs host wall clock | 5.004 / 5.005 / 5.012 s | 5.005 / 5.004 / 5.004 s |
| UART RX echo — 64 B | PASS, byte-for-byte | PASS, byte-for-byte |
| UART RX echo — 2000 B, 64-byte chunks | PASS, byte-for-byte | PASS, byte-for-byte |
| Interrupt load — 8000 B sustained | PASS, byte-for-byte, 6.5 KiB/s | PASS, byte-for-byte, 6.5 KiB/s |
| Firmware counters | **`rx=10065 isr=10065`** | **`rx=10065 isr=10065`** |
| Runtime reconfigure on the wire | PASS incl. negative control | PASS incl. negative control |
| `tests/drivers/uart/uart_basic_api` | PASS 7/7 | PASS 7/7 |
| `tests/drivers/uart/uart_interrupt_api` | PASS 1/1 | PASS 1/1 |
| Cell shutdown/restart cycling | PASS 6/6 | PASS 6/6 |
| `mtk-v4.4.2` GIC restore boots | PASS (native) | PASS (cross-board) |

The two parts agree to the decimal on throughput and to within 8 ms on timing. Nothing is
board-specific in the driver behaviour.

Worth recording: the `uart_basic_api` and `uart_interrupt_api` images are **byte-identical between
the two boards** — md5 `823337971d33…` and `8fd93b8368…` respectively. Those ztest suites never print
`CONFIG_BOARD_TARGET`, and both boards resolve to the same Kconfig and the same enabled-CPU set, so
the images come out the same. Only the `hello_world`-derived images differ, and only in the board
string.

### How each test works, and what it does *not* prove

| Test | Method | Caveat |
|---|---|---|
| `cntfrq` | `printk("cntfrq = %u\n", sys_clock_hw_cycles_per_sec())` at boot | reads what the driver believes, which with `TIMER_READS_ITS_FREQUENCY_AT_RUNTIME` is `CNTFRQ_EL0` |
| Timer accuracy | firmware prints `SLEEP_START`/`SLEEP_END` around `k_sleep(K_SECONDS(5))`; **host** timestamps arrival | deliberately not `k_uptime_get()`, which derives from the timer under test and cannot detect a misconfigured one |
| RX / interrupt load | firmware echoes every byte from an interrupt callback, counting bytes and ISR entries; host compares the echo byte-for-byte and then requests counters with an out-of-band `0x04` that is counted but not echoed | the echo happens inside the ISR via `uart_poll_out`, so throughput is not a driver performance ceiling |
| Runtime reconfigure | firmware moves the console 115200 → 9600 → 115200, announcing each phase repeatedly; host samples at each rate | the **negative** control is the real evidence: phase 2 is unreadable at 115200. Without it, a `uart_configure()` that returned 0 and did nothing would look identical |
| `uart_basic_api` | declared `harness: keyboard`, so twister cannot run it — the host supplies keystrokes over the same serial line | — |
| `uart_interrupt_api` | same driver method | upstream scopes it `vendor_allow: adi`, so it is **not** a suite our board is expected to run; passing is a bonus, not a gate |
| Cell cycling | six shutdown → destroy → create → load → start cycles, counting banners | — |

**`isr == rx` exactly on both boards**: one interrupt per received byte, so the RX FIFO trigger is
effectively 1 and nothing batches. Not a bug. It does mean RX is interrupt-bound — 6.5 KiB/s against
an 11.5 KiB/s line rate — so raising the FIFO trigger level is where to look if RX throughput ever
matters.

### Suites not run, and why

The rest of `tests/drivers/uart/` is `uart_async_api`, `uart_async_dual`, `uart_async_rx`,
`uart_async_slip`, `uart_elementary`, `uart_emul`, `uart_errors`, `uart_mix_fifo_poll` and `uart_pm`.
The driver implements the **interrupt-driven** API, not the async one, and the board exposes a single
console UART with no loopback or second port. Those are **not applicable**, not untested.

---

## 2. The defconfig fix, folded into commits 14 and 15

```
-CONFIG_DCACHE_LINE_SIZE_DETECT=y
-CONFIG_ICACHE_LINE_SIZE_DETECT=y
```

removed from both board defconfigs. `CONFIG_CACHE_MANAGEMENT=y` on the line above is untouched — that
is the one doing real work.

**Verified as a functional no-op rather than assumed:**

| Check | Result |
|---|---|
| `.config` diff, before vs after, both boards | **0 differing lines** |
| `CONFIG_DCACHE_LINE_SIZE` / `ICACHE_LINE_SIZE` | still **64** on both, correct for Cortex-A55 |
| Kconfig cache-detect warnings per build | **2 → 0** on both boards |
| Diff vs the pre-fix tip | exactly 4 deleted lines across 2 files, nothing else |
| Boot after the change | confirmed on the 510: `v4.4.0-13857-g6e09950bf79d`, correct board string |

arm64 has no runtime cache-line-size detection, so `*_LINE_SIZE_DETECT_SUPPORT` is `n` and both
symbols were being forced off — the lines were dead and warned on every single build.

### Gates after the fold

| Gate | Result |
|---|---|
| both Arm boards build | PASS |
| 5 ADSP targets | PASS |
| ADSP neutrality | 0 removed / 3 added, **loadable binaries byte-identical** on all five |
| `check_compliance.py -c origin/main..HEAD` | **exit 0**, 1 warning (PINCTRL, accepted) |
| `checkpatch.pl -g origin/main..HEAD` | 0 errors, 0 warnings |
| commits / identities | 19, all `aary.patil@mediatek.com` |

---

## 3. `TODO(6)` answered for both boards — `jailhouse enable` is required

Your doc TODO asks whether `jailhouse enable` is needed or the image brings the root cell up.
**It is required, on both boards, from cold.** Confirmed independently on each:

- **Genio 700:** at power-on the module was not loaded and `jailhouse cell list` was empty.
- **Genio 510:** confirmed the harder way. My original `setup-g510.sh` had **no** enable step and
  worked anyway, because jailhouse happened to already be running from an earlier session. After the
  board was power-cycled it failed.

So the doc can state it unconditionally for both boards. The sequence is `modprobe jailhouse`,
`jailhouse enable /usr/share/jailhouse/cells/genio-<part>-evk.cell`, then create/load/start.

### A trap that belongs in the doc next to it

**`jailhouse cell list` exits 0 even when jailhouse is not enabled** — it simply prints nothing. A
guard written as

```sh
jailhouse cell list >/dev/null 2>&1 || jailhouse enable ...     # WRONG
```

therefore skips the enable, and the failure surfaces two commands later as

```
JAILHOUSE_CELL_CREATE: Invalid argument
```

which reads like a malformed cell config rather than a missing root cell. Both setup scripts now use

```sh
jailhouse cell list 2>/dev/null | grep -qE "^[0-9]" || jailhouse enable ...
```

This cost me a diagnosis cycle on each board and would cost a reader the same.

---

## 4. Scripts, all in `scripts/` on this branch

| File | Side | Purpose |
|---|---|---|
| `hwtest-main.c` | firmware | drop-in `samples/hello_world/src/main.c`: `cntfrq` + three `SLEEP_START`/`SLEEP_END` pairs |
| `rxtest-main.c` | firmware | interrupt-driven echo, `rx`/`isr` counters, `0x04` requests a `STATS` line |
| `reconf-main.c` | firmware | `uart_config_get`, then 115200 → 9600 → 115200 with repeated phase announcements |
| `rxtest-host.py` | host | sends known data, verifies the echo byte-for-byte, reads the counters |
| `reconf-host.py` | host | samples at each baud, including the negative control |
| `uartapi-host.py` | host | drives `uart_basic_api`, answering its keyboard prompts. `SETUP=` and `IMAGE=` env vars select the board |
| `uartlog.py` | host | timestamps each line on arrival — the external clock the timer test needs |
| `setup-g700.sh`, `setup-g510.sh` | board | modprobe, enable if needed, create/load/start |
| `board-README.md`, `board-README-g700.md` | board | copies of what is in `/root/claude_aary` on each |

Conditions that matter when re-running:

- **Only one process can hold `/dev/ttyUSB0`** (`picocom` is built with `USE_FLOCK`). The line logger
  and the host test scripts cannot run at once — kill the logger before an interactive test.
- The two boards have **different FTDI adapters** — `AB0PKARP` (700) and `B001I8ZJ` (510) — but both
  enumerate as `/dev/ttyUSB0`. Use `/dev/serial/by-id/` if both are ever attached together.
- `rxtest-host.py` and `reconf-host.py` do **not** restart the cell; load the image first.
  `uartapi-host.py` does restart it, via `SETUP`/`IMAGE`.

---

## 5. Board state

**Genio 510** (currently attached), `/root/claude_aary`, all images from `6e09950bf79`:

| File | md5 |
|---|---|
| `zephyr-g510.bin` | `ec20d80566cc147fede86360a820cdbf` |
| `zephyr-g510-cntfrq.bin` | `640dbcb662e8012fb3306d3d275c614a` |
| `zephyr-g510-rxtest.bin` | `cfff20bd4d51bc8144f3b50029a0fe94` |
| `zephyr-g510-reconf.bin` | `145ef1bd0697640bdd75649134e75625` |
| `zephyr-g510-uartapi.bin` | `823337971d33a1e6b36dc780ac6bebad` |
| `zephyr-g510-uartirq.bin` | `8fd93b83688e266f4e14120ebd5013f5` |

**Genio 700** (not attached), `/root/claude_aary`: equivalents built from `0d9156ec50b`. Those predate
the defconfig fold, so their `.config` is identical but the embedded SHA differs. Since the fix is a
proven no-op, the 700 results stand; refresh the images before any *new* 700 report so the md5
belongs to the submitted history.

Both boards keep their own `README.md` and `setup-*.sh`.

---

## 6. Outstanding

- **Docs:** `TODO(6)` is now answerable for both boards, see §3. That leaves the cell filenames and
  where to obtain a Jailhouse-enabled image — both still MediaTek-internal facts.
- **Photo licensing:** still needs Aary's explicit yes before the PR opens.
- **`verified/*` tags** shadow the SHA in the boot banner via `git describe`; lightweight tags would
  keep both.
- **SMP / multiple A55 cores:** deferred. Needs a Jailhouse cell assigning more than one CPU, which
  is a MediaTek-side change, plus enabling the disabled cores in the board dts and raising
  `CONFIG_MP_MAX_NUM_CPUS`.
- **`mtk-v4.4.2` migration list:** the 6-core dtsi, the GIC entries that must *not* converge, the 510
  board and docs, commit 10's message, and now this defconfig fix.
