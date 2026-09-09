#!/usr/bin/env bash
#
# MediaTek Genio Zephyr test suite.
#
# Two tiers:
#   gates    build and static checks — no board needed
#   hardware on-board tests — needs a Genio EVK over adb plus its UART on serial
#
# Everything is built from the current checkout, so a run actually exercises
# your changes rather than whatever was last flashed.
#
#   ./run-tests.sh                 # everything available
#   ./run-tests.sh --gates         # gates only
#   ./run-tests.sh --hardware      # hardware only
#   ./run-tests.sh --list          # show what would run
#
set -uo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
. "$TESTS_DIR/lib/common.sh"

RUN_GATES=1
RUN_HW=1
ADSP_NEUTRALITY=0
LIST_ONLY=0

usage() { sed -n '2,20p' "$0" | sed 's/^# \?//'; exit 0; }

while [ $# -gt 0 ]; do
	case "$1" in
	--gates)      RUN_HW=0 ;;
	--hardware)   RUN_GATES=0 ;;
	--adsp)       ADSP_NEUTRALITY=1 ;;
	--list)       LIST_ONLY=1 ;;
	--board)      shift; BOARD_TARGET="$1"; BOARD_TAG=custom ;;
	--port)       shift; PORT="$1" ;;
	-h|--help)    usage ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

mkdir -p "$LOG_DIR" "$BUILD_ROOT"
UART_LOG="$LOG_DIR/uart.log"
activate_venv

if [ "$LIST_ONLY" = 1 ]; then
	cat <<'EOF'
gates (no board):
  G1  build both Arm board targets
  G2  compliance         check_compliance.py -c <base>..HEAD
  G3  checkpatch         checkpatch.pl -g <base>..HEAD
  G4  ADSP neutrality    five DSP targets byte-identical to base   [--adsp only, slow]
hardware (board + serial):
  H1  boot and board string
  H2  cntfrq and k_sleep accuracy
  H3  UART RX echo and interrupt load
  H4  runtime UART reconfigure
  H5  tests/drivers/uart/uart_basic_api
  H6  tests/drivers/uart/uart_interrupt_api
  H7  cell shutdown/restart cycling
EOF
	exit 0
fi

cd "$ZEPHYR_BASE" || { echo "no zephyr tree at $ZEPHYR_BASE" >&2; exit 2; }
BASE="$(git rev-parse origin/main 2>/dev/null || echo '')"
HEAD_SHA="$(git rev-parse --short HEAD)"

log "MediaTek Genio Zephyr test suite"
log "  tree     $ZEPHYR_BASE @ $HEAD_SHA ($(git rev-parse --abbrev-ref HEAD))"

if [ "$RUN_HW" = 1 ]; then
	if detect_board; then
		log "  board    $BOARD_HOST -> $BOARD_TARGET"
	else
		log "  board    none detected — hardware tests will be skipped"
		RUN_HW=0
	fi
fi
[ "$RUN_GATES" = 1 ] && log "  base     ${BASE:-<no origin/main>}"

############################## gates ##################################
if [ "$RUN_GATES" = 1 ]; then
	hdr "gates (no board required)"

	for bt in mt8390_genio_700_evk/mt8188/a55 mt8370_genio_510_evk/mt8188/a55; do
		short="${bt%%/*}"
		if west build -p always -b "$bt" samples/hello_world \
			-d "$BUILD_ROOT/gate_${short}" >"$LOG_DIR/gate_$short.log" 2>&1; then
			ram="$(grep -oP 'RAM:\s+\K[0-9]+ [KM]B\s+[0-9]+ [KM]B' "$LOG_DIR/gate_$short.log" | head -1)"
			pass "G1 build $short" "${ram:-}"
		else
			fail "G1 build $short" "see $LOG_DIR/gate_$short.log"
		fi
	done

	if [ -n "$BASE" ]; then
		if ./scripts/ci/check_compliance.py -c "$BASE..HEAD" >"$LOG_DIR/compliance.log" 2>&1; then
			pass "G2 compliance" "$(grep -Eo '[0-9]+ check\(s\) with warnings only' "$LOG_DIR/compliance.log" | head -1)"
		else
			fail "G2 compliance" "$(grep -Eo '[0-9]+ check\(s\) failed' "$LOG_DIR/compliance.log" | head -1)"
		fi

		if ./scripts/checkpatch.pl -g "$BASE..HEAD" >"$LOG_DIR/checkpatch.log" 2>&1; then
			pass "G3 checkpatch" "0 errors, 0 warnings"
		else
			fail "G3 checkpatch" "$(grep -c 'has style problems' "$LOG_DIR/checkpatch.log") commit(s) with problems"
		fi
	else
		skip "G2 compliance" "no origin/main to diff against"
		skip "G3 checkpatch" "no origin/main to diff against"
	fi

	if [ "$ADSP_NEUTRALITY" = 1 ] && [ -n "$BASE" ]; then
		# The SoC reorganisation must not change the audio DSP platforms. A
		# config diff alone is weak, so compare the loadable images: expect
		# exactly three ADDED symbols and ZERO removed. Any removal means a
		# DSP lost XTENSA or its Kconfig.defconfig body, which no compile
		# error would catch.
		oc="$(ls /home/lab/zephyr-sdk-*/gnu/xtensa-*/bin/*objcopy 2>/dev/null | head -1)"
		for t in mt8186/mt8186/adsp mt8188/mt8188/adsp mt8195/mt8195/adsp \
		         mt8196/mt8196/adsp mt8365/mt8365/adsp; do
			d="${t//\//_}"
			west build -p always -b "$t" samples/hello_world -d "$BUILD_ROOT/adsp_new_$d" \
				>"$LOG_DIR/adsp_new_$d.log" 2>&1 || { fail "G4 adsp $t" "build failed"; continue; }
			git checkout --detach "$BASE" >/dev/null 2>&1
			west build -p always -b "$t" samples/hello_world -d "$BUILD_ROOT/adsp_base_$d" \
				>"$LOG_DIR/adsp_base_$d.log" 2>&1
			git checkout - >/dev/null 2>&1
			rem=$(diff "$BUILD_ROOT/adsp_base_$d/zephyr/.config" "$BUILD_ROOT/adsp_new_$d/zephyr/.config" | grep -c '^<')
			bm=$("$oc" -O binary "$BUILD_ROOT/adsp_base_$d/zephyr/zephyr.elf" /dev/stdout 2>/dev/null | md5sum | cut -d' ' -f1)
			nm=$("$oc" -O binary "$BUILD_ROOT/adsp_new_$d/zephyr/zephyr.elf" /dev/stdout 2>/dev/null | md5sum | cut -d' ' -f1)
			if [ "$rem" = "0" ] && [ "$bm" = "$nm" ]; then
				pass "G4 adsp $t" "0 removed, binary identical"
			else
				fail "G4 adsp $t" "$rem config line(s) removed, binary $([ "$bm" = "$nm" ] && echo same || echo DIFFERS)"
			fi
		done
	elif [ "$ADSP_NEUTRALITY" = 1 ]; then
		skip "G4 adsp neutrality" "no origin/main"
	fi
fi

############################## hardware ###############################
if [ "$RUN_HW" = 1 ]; then
	hdr "hardware ($BOARD_HOST)"

	if ! start_logger; then
		skip "H1..H7 (all hardware tests)" "$LOGGER_ERR"
		summary; exit $?
	fi

	# --- H1/H2: boot, board string, cntfrq, timer -----------------------
	if build_image "$TESTS_DIR/firmware/hwtest" hwtest; then
		info "    hwtest  md5 $IMAGE_MD5  ${IMAGE_RAM:-}"
		if flash_image "$IMAGE_BIN" "zephyr-$BOARD_TAG-hwtest.bin"; then
			: > "$UART_LOG"; run_cell "zephyr-$BOARD_TAG-hwtest.bin"; sleep 20
			if grep -q "Hello World! $BOARD_TARGET" "$UART_LOG"; then
				pass "H1 boot and board string" "$(grep -o 'build [^ ]*' "$UART_LOG" | head -1)"
			else
				fail "H1 boot and board string" "cell=$(cell_state); see $UART_LOG"
			fi

			cf="$(grep -oP 'cntfrq = \K[0-9]+' "$UART_LOG" | head -1)"
			[ "$cf" = "13000000" ] && pass "H2a cntfrq" "$cf Hz" \
				|| fail "H2a cntfrq" "got '${cf:-nothing}', expected 13000000"

			# Measured host-side on purpose; see uartlog.py.
			worst="$(python3 - "$UART_LOG" <<'PY'
import re, sys
s, e = {}, {}
for l in open(sys.argv[1]):
    m = re.match(r'\[(\d+\.\d+)\] SLEEP_START (\d+)', l)
    if m: s[m.group(2)] = float(m.group(1))
    m = re.match(r'\[(\d+\.\d+)\] SLEEP_END\s+(\d+)', l)
    if m: e[m.group(2)] = float(m.group(1))
d = [abs(e[k]-s[k]-5.0) for k in s if k in e]
print(f"{max(d)*1000:.0f}" if d else "")
PY
)"
			if [ -n "$worst" ] && [ "$worst" -lt 100 ] 2>/dev/null; then
				pass "H2b k_sleep(5s) accuracy" "worst deviation ${worst} ms"
			else
				fail "H2b k_sleep(5s) accuracy" "worst ${worst:-no samples} ms"
			fi
		else
			fail "H1 boot and board string" "image transfer md5 mismatch"
		fi
	else
		fail "H1/H2 hwtest" "build failed"
	fi

	# --- H3: RX echo and interrupt load ---------------------------------
	if build_image "$TESTS_DIR/firmware/rxtest" rxtest \
	   && flash_image "$IMAGE_BIN" "zephyr-$BOARD_TAG-rxtest.bin"; then
		run_cell "zephyr-$BOARD_TAG-rxtest.bin"; sleep 3
		stop_logger
		if PORT="$PORT" python3 "$TESTS_DIR/host/rxtest_host.py" >"$LOG_DIR/rxtest.log" 2>&1; then
			pass "H3 UART RX + interrupt load" "$(grep -oP 'STATS \K.*' "$LOG_DIR/rxtest.log" | head -1)"
		else
			fail "H3 UART RX + interrupt load" "see $LOG_DIR/rxtest.log"
		fi
	else
		fail "H3 UART RX + interrupt load" "build or flash failed"
		stop_logger
	fi

	# --- H4: runtime reconfigure ----------------------------------------
	if build_image "$TESTS_DIR/firmware/reconf" reconf \
	   && flash_image "$IMAGE_BIN" "zephyr-$BOARD_TAG-reconf.bin"; then
		run_cell "zephyr-$BOARD_TAG-reconf.bin"
		if PORT="$PORT" python3 "$TESTS_DIR/host/reconf_host.py" >"$LOG_DIR/reconf.log" 2>&1 \
		   && grep -q 'runtime reconfigure works on the wire' "$LOG_DIR/reconf.log"; then
			pass "H4 runtime reconfigure" "115200 -> 9600 -> 115200, incl. negative control"
		else
			fail "H4 runtime reconfigure" "see $LOG_DIR/reconf.log"
		fi
	else
		fail "H4 runtime reconfigure" "build or flash failed"
	fi

	# --- H5/H6: in-tree ztest suites ------------------------------------
	# uart_basic_api is declared `harness: keyboard`, so twister cannot run it;
	# uartapi_host.py supplies the keystrokes over the same serial line.
	for spec in "H5 uart_basic_api:tests/drivers/uart/uart_basic_api:uartapi" \
	            "H6 uart_interrupt_api:tests/drivers/uart/uart_interrupt_api:uartirq"; do
		label="${spec%%:*}"; rest="${spec#*:}"; src="${rest%%:*}"; nm="${rest##*:}"
		if build_image "$ZEPHYR_BASE/$src" "$nm" \
		   && flash_image "$IMAGE_BIN" "zephyr-$BOARD_TAG-$nm.bin"; then
			if SETUP="$BOARD_SETUP" IMAGE="zephyr-$BOARD_TAG-$nm.bin" PORT="$PORT" \
			   python3 "$TESTS_DIR/host/uartapi_host.py" >"$LOG_DIR/$nm.log" 2>&1; then
				pass "$label" "$(grep -oP 'SUITE PASS[^]]*\]' "$LOG_DIR/$nm.log" | head -1)"
			else
				fail "$label" "see $LOG_DIR/$nm.log"
			fi
		else
			fail "$label" "build or flash failed"
		fi
	done

	# --- H7: cell restart cycling ---------------------------------------
	start_logger || true
	: > "$UART_LOG"
	cycles=6; booted=0
	for _ in $(seq $cycles); do
		run_cell "zephyr-$BOARD_TAG-hwtest.bin" >/dev/null 2>&1
		sleep 2
	done
	sleep 2
	booted="$(grep -c 'Booting Zephyr OS' "$UART_LOG")"
	[ "$booted" -eq "$cycles" ] \
		&& pass "H7 cell restart cycling" "$booted/$cycles clean boots" \
		|| fail "H7 cell restart cycling" "$booted/$cycles boots seen"
	stop_logger
fi

summary
