#!/bin/sh
# Genio 510 EVK - Zephyr inmate cell.
# Usage: ./setup-g510.sh [image]      default zephyr-g510.bin
#
# "jailhouse cell list" exits 0 even when jailhouse is not enabled, so the guard
# below tests for an actual cell row rather than the exit code. Without the
# enable, "cell create" fails with "JAILHOUSE_CELL_CREATE: Invalid argument",
# which points away from the real cause.
set -e
cd /root/claude_aary
modprobe jailhouse 2>/dev/null || true
if ! jailhouse cell list 2>/dev/null | grep -qE "^[0-9]"; then
    jailhouse enable /usr/share/jailhouse/cells/genio-510-evk.cell
fi
jailhouse cell shutdown zephyr 2>/dev/null || true
jailhouse cell destroy  zephyr 2>/dev/null || true
jailhouse cell create /usr/share/jailhouse/cells/genio-510-evk-zephyr.cell
jailhouse cell load   zephyr "${1:-zephyr-g510.bin}" -a 0x00008000
jailhouse cell start  zephyr
jailhouse cell list
