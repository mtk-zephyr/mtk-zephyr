#!/bin/sh
# Genio 510 EVK - matching Zephyr image in the matching inmate cell.
# The original setup.sh creates the genio-700 cell and loads the genio-700
# image; both are wrong for this board. Kept alongside, not modified.
set -e
cd /root/claude_aary
modprobe jailhouse 2>/dev/null || true
jailhouse cell shutdown zephyr 2>/dev/null || true
jailhouse cell destroy  zephyr 2>/dev/null || true
jailhouse cell create /usr/share/jailhouse/cells/genio-510-evk-zephyr.cell
jailhouse cell load   zephyr "${1:-zephyr-g510.bin}" -a 0x00008000
jailhouse cell start  zephyr
jailhouse cell list
