# shellcheck shell=bash
# Shared helpers for the MediaTek Genio Zephyr test suite.
# Sourced by run-tests.sh; not executable on its own.

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZEPHYR_BASE="${ZEPHYR_BASE:-$HOME/zephyrproject/zephyr}"
VENV="${VENV:-$HOME/zephyrproject/.venv}"
BOARD_DIR="${BOARD_DIR:-/root/claude_aary}"
PORT="${PORT:-/dev/ttyUSB0}"
BAUD=115200
BUILD_ROOT="${BUILD_ROOT:-$ZEPHYR_BASE/build/tests}"
LOG_DIR="${LOG_DIR:-$TESTS_DIR/logs}"
LOGGER_ERR=

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
declare -a RESULTS=()

c_red=$'\033[31m'; c_grn=$'\033[32m'; c_yel=$'\033[33m'; c_dim=$'\033[2m'; c_off=$'\033[0m'
[ -t 1 ] || { c_red=; c_grn=; c_yel=; c_dim=; c_off=; }

log()  { printf '%s\n' "$*"; }
info() { printf '%s%s%s\n' "$c_dim" "$*" "$c_off"; }
hdr()  { printf '\n=== %s\n' "$*"; }

pass() { PASS_COUNT=$((PASS_COUNT+1)); RESULTS+=("PASS|$1|$2"); printf '  %sPASS%s  %-38s %s\n' "$c_grn" "$c_off" "$1" "${2:-}"; }
fail() { FAIL_COUNT=$((FAIL_COUNT+1)); RESULTS+=("FAIL|$1|$2"); printf '  %sFAIL%s  %-38s %s\n' "$c_red" "$c_off" "$1" "${2:-}"; }
skip() { SKIP_COUNT=$((SKIP_COUNT+1)); RESULTS+=("SKIP|$1|$2"); printf '  %sSKIP%s  %-38s %s\n' "$c_yel" "$c_off" "$1" "${2:-}"; }

activate_venv() {
	# shellcheck disable=SC1091
	[ -f "$VENV/bin/activate" ] && . "$VENV/bin/activate"
}

# --- board identification -------------------------------------------------
# Returns via globals: BOARD_HOST, BOARD_TARGET, BOARD_SETUP, BOARD_TAG.
detect_board() {
	if ! command -v adb >/dev/null 2>&1; then
		return 1
	fi
	BOARD_HOST="$(adb shell 'uname -n' 2>/dev/null | tr -d '\r\n')"
	case "$BOARD_HOST" in
	genio-700-evk)
		BOARD_TARGET=mt8390_genio_700_evk/mt8188/a55
		BOARD_SETUP="$BOARD_DIR/setup-g700.sh"; BOARD_TAG=g700 ;;
	genio-510-evk)
		BOARD_TARGET=mt8370_genio_510_evk/mt8188/a55
		BOARD_SETUP="$BOARD_DIR/setup-g510.sh"; BOARD_TAG=g510 ;;
	*)
		return 1 ;;
	esac
	return 0
}

# --- serial port ----------------------------------------------------------
# Only one process may hold the port: picocom is built with USE_FLOCK, and the
# line logger and the host test drivers cannot both own it.
port_holder()  { fuser "$PORT" 2>/dev/null | tr -d ' '; }
release_port() {
	local pids; pids="$(port_holder)"
	[ -n "$pids" ] && kill $pids 2>/dev/null
	sleep 1
	[ -z "$(port_holder)" ]
}

LOGGER_PID=
# Distinguish the three ways this goes wrong, because they need different fixes:
# the adapter is unplugged, another process holds the port, or the logger died.
start_logger() {
	if [ ! -e "$PORT" ]; then
		LOGGER_ERR="$PORT does not exist — serial adapter unplugged?"
		return 1
	fi
	if ! release_port; then
		LOGGER_ERR="$PORT is held by PID(s) $(port_holder) — fuser -k $PORT"
		return 1
	fi
	stty -F "$PORT" $BAUD raw -echo -crtscts 2>>"$LOG_DIR/logger.log" || {
		LOGGER_ERR="could not configure $PORT (see $LOG_DIR/logger.log)"
		return 1
	}
	: > "$UART_LOG"
	python3 "$TESTS_DIR/host/uartlog.py" --port "$PORT" --log "$UART_LOG" \
		>>"$LOG_DIR/logger.log" 2>&1 &
	LOGGER_PID=$!
	sleep 2
	# An absent console is ambiguous on this platform: "no output" is both a
	# real failure signature and what a dead capture looks like. Confirm the
	# logger actually holds the port before trusting any silence.
	if [ -z "$(port_holder)" ]; then
		LOGGER_ERR="logger did not attach (see $LOG_DIR/logger.log)"
		return 1
	fi
	return 0
}
stop_logger() {
	[ -n "$LOGGER_PID" ] && kill "$LOGGER_PID" 2>/dev/null
	LOGGER_PID=
	sleep 1
}

# --- build / flash --------------------------------------------------------
# $1 = source dir (abs or relative to ZEPHYR_BASE), $2 = short name
build_image() {
	local src="$1" name="$2" d="$BUILD_ROOT/$2_$BOARD_TAG"
	west build -p always -b "$BOARD_TARGET" "$src" -d "$d" \
		>"$LOG_DIR/build_$name.log" 2>&1 || return 1
	IMAGE_BIN="$d/zephyr/zephyr.bin"
	IMAGE_MD5="$(md5sum "$IMAGE_BIN" | cut -d' ' -f1)"
	IMAGE_RAM="$(grep -oP 'RAM:\s+\K[0-9]+ [KM]B\s+[0-9]+ [KM]B' "$LOG_DIR/build_$name.log" | head -1)"
	return 0
}

# $1 = local .bin, $2 = remote basename. Verifies the transfer by md5, because
# a stale or truncated image is otherwise indistinguishable from a regression.
flash_image() {
	local bin="$1" remote="$2"
	adb push "$bin" "$BOARD_DIR/$remote" >/dev/null 2>&1 || return 1
	local want got
	want="$(md5sum "$bin" | cut -d' ' -f1)"
	got="$(adb shell "md5sum $BOARD_DIR/$remote" 2>/dev/null | cut -d' ' -f1 | tr -d '\r')"
	[ "$want" = "$got" ]
}

# CELL_DIR lets a run use cell configs from somewhere other than
# /usr/share/jailhouse/cells — e.g. newly built ones staged under $BOARD_DIR.
run_cell() {
	adb shell "${CELL_DIR:+CELL_DIR=$CELL_DIR }$BOARD_SETUP $1" >/dev/null 2>&1
}

cell_state() {
	adb shell 'jailhouse cell list' 2>/dev/null | awk '/zephyr/{print $3}' | tr -d '\r'
}

# --- misc -----------------------------------------------------------------
require() {
	local what="$1"; shift
	command -v "$1" >/dev/null 2>&1 || { info "missing $what ($1)"; return 1; }
}

summary() {
	printf '\n%s\n' "------------------------------------------------------------"
	printf '  %spassed%s %d   %sfailed%s %d   %sskipped%s %d\n' \
		"$c_grn" "$c_off" "$PASS_COUNT" "$c_red" "$c_off" "$FAIL_COUNT" \
		"$c_yel" "$c_off" "$SKIP_COUNT"
	if [ "$FAIL_COUNT" -gt 0 ]; then
		printf '\n  failures:\n'
		for r in "${RESULTS[@]}"; do
			case "$r" in FAIL\|*) printf '    %s  %s\n' "${r#FAIL|}" ;; esac
		done | sed 's/|/  /'
	fi
	printf '  logs: %s\n' "$LOG_DIR"
	[ "$FAIL_COUNT" -eq 0 ]
}
