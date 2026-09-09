#!/bin/sh
# Genio 510 EVK — bring up the Zephyr inmate cell.
#
# Usage: ./setup-g510.sh [image]        default zephyr-g510.bin
#
# Two things this works around, both of which cost a debugging cycle once:
#
#  1. "jailhouse cell list" exits 0 even when jailhouse is NOT enabled — it just
#     prints nothing. A guard written as `jailhouse cell list >/dev/null || enable`
#     therefore skips the enable, and the failure appears two commands later as
#     "JAILHOUSE_CELL_CREATE: Invalid argument", which points at the cell config
#     rather than at the missing root cell. Test for an actual cell row instead.
#  2. jailhouse enable IS required from cold on both boards; nothing in the image
#     brings the root cell up.
set -e

DIR=${BOARD_DIR:-/root/claude_aary}
IMAGE=${1:-zephyr-g510.bin}
CELLS=${CELL_DIR:-/usr/share/jailhouse/cells}

cd "$DIR"

modprobe jailhouse 2>/dev/null || true

if ! jailhouse cell list 2>/dev/null | grep -qE "^[0-9]"; then
	jailhouse enable $CELLS/genio-510-evk.cell
fi

jailhouse cell shutdown zephyr 2>/dev/null || true
jailhouse cell destroy  zephyr 2>/dev/null || true

jailhouse cell create $CELLS/genio-510-evk-zephyr.cell
jailhouse cell load   zephyr "$IMAGE" -a 0x00008000
jailhouse cell start  zephyr

jailhouse cell list
