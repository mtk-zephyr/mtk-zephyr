#!/bin/sh
# Genio 700 EVK - Zephyr inmate cell.
# Usage: ./setup-g700.sh [image]      default zephyr-g700.bin
#
# Note: "jailhouse cell list" exits 0 even when jailhouse is not enabled, so the
# guard below tests for an actual cell row rather than the exit code.
set -e
cd /root/claude_aary
modprobe jailhouse 2>/dev/null || true
if ! jailhouse cell list 2>/dev/null | grep -qE "^[0-9]"; then
    jailhouse enable /usr/share/jailhouse/cells/genio-700-evk.cell
fi
jailhouse cell shutdown zephyr 2>/dev/null || true
jailhouse cell destroy  zephyr 2>/dev/null || true
jailhouse cell create /usr/share/jailhouse/cells/genio-700-evk-zephyr.cell
jailhouse cell load   zephyr "${1:-zephyr-g700.bin}" -a 0x00008000
jailhouse cell start  zephyr
jailhouse cell list
