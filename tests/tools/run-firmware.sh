#!/bin/bash
# Flash one firmware image to the attached board and capture its console.
#
#   tools/run-firmware.sh <image.bin> <done-pattern> [tag]
#
# SETUP names the board-side bring-up script, defaulting to the one for the
# detected board.  Point it at setup-afe.sh to run under the cell that grants
# the Audio Front End:
#
#   SETUP=setup-afe.sh tools/run-firmware.sh build/zephyr/zephyr.bin 'PASS --'
#
# For running a single test image by hand, outside a full suite run. The suite
# does this internally; this is for when you want one firmware and one console
# transcript without the rest of the tier.
#
# The logger lifecycle is the whole point of this script existing.
# `setsid python3 uartlog.py ... &` makes $! the pid of setsid, which exits
# immediately, so every kill misses and the logger is orphaned still holding the
# serial port. Several readers on one port each receive a fraction of the bytes,
# which is indistinguishable from a board that booted to a dead console. That
# cost a whole bisection once: three separate branches were each judged not to
# boot, and all three were fine. Hence: no setsid, kill on exit, and refuse to
# start at all if something else holds the port.
set -u

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../lib/common.sh
. "$TESTS_DIR/lib/common.sh"

export PATH="$PATH:/home/lab/platform-tools"

IMAGE="${1:?usage: run-firmware.sh <image.bin> <done-pattern> [tag]}"
PATTERN="${2:?usage: run-firmware.sh <image.bin> <done-pattern> [tag]}"
TAG="${3:-$(basename "$IMAGE" .bin)}"
LOG="$LOG_DIR/$TAG.log"

holder="$(fuser "$PORT" 2>/dev/null | tr -d ' ')"
if [ -n "$holder" ]; then
	echo "$PORT is held by pid $holder -- refusing to run." >&2
	echo "A second reader steals bytes and the result would be meaningless." >&2
	exit 1
fi

detect_board || { echo "no board detected" >&2; exit 1; }

SETUP_PATH="$BOARD_SETUP"
if [ -n "${SETUP:-}" ]; then
	SETUP_PATH="$BOARD_DIR/$(basename "$SETUP")"
fi

# Upload before running.  Only the basename reaches the board script, so
# without this the run would use whatever image already carried that name --
# silently testing a stale binary, which reads as a result rather than a
# mistake.
REMOTE="$(basename "$IMAGE")"
flash_image "$IMAGE" "$REMOTE" || { echo "could not upload $IMAGE" >&2; exit 1; }

echo "board  $BOARD_HOST -> $BOARD_TARGET"
echo "image  $IMAGE"
echo "setup  $SETUP_PATH"
echo "log    $LOG"
echo

rm -f "$LOG"
python3 "$TESTS_DIR/host/uartlog.py" --port "$PORT" --log "$LOG" >/dev/null 2>&1 &
LP=$!
trap 'kill "$LP" 2>/dev/null' EXIT
sleep 3

adb shell "${CELL_DIR:+CELL_DIR=$CELL_DIR }$SETUP_PATH $REMOTE" >/dev/null 2>&1

for _ in $(seq 1 30); do
	grep -q "$PATTERN" "$LOG" 2>/dev/null && break
	sleep 2
done

kill "$LP" 2>/dev/null
wait "$LP" 2>/dev/null
trap - EXIT
sleep 1

sed 's/^\[[0-9.]*\] //' "$LOG" 2>/dev/null | sed 's/^/  /'

left="$(fuser "$PORT" 2>/dev/null | tr -d ' ')"
echo
echo "port after: ${left:-free}"
grep -q "$PATTERN" "$LOG" 2>/dev/null
