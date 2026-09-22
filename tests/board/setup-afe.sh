#!/bin/sh
# Bring up the Zephyr inmate under the cell that grants the Audio Front End.
#
# Usage: ./setup-afe.sh <image>
#
# The AFE sits in the MTCMOS "audio" power domain, which is off on a stock boot
# and which neither the cell nor the Zephyr driver can turn on -- no SPM region
# is granted.  Holding Linux's runtime-PM reference on the AFE device keeps the
# domain powered for the inmate.  Without it every register write is dropped,
# every driver call still returns 0, and the only symptom is a DMA pointer
# outside the buffer.
#
# The cell is 700-authored and used on the 510 as well: same die family, AFE at
# the same addresses, identical inmate window and console.  Only the root cell
# differs, so that one comes from the board.
set -e

DIR=${BOARD_DIR:-/root/claude_aary}
IMAGE=${1:?usage: setup-afe.sh <image>}
CELLS=${CELL_DIR:-/usr/share/jailhouse/cells}
ROOT_CELL=${ROOT_CELL:-genio-510-evk.cell}
INMATE_CELL=${INMATE_CELL:-genio-700-evk-zephyr-afe.cell}

# The AFE cell is not part of the stock rootfs -- it was hand-added, and a
# reflash takes it with everything else in /usr/share/jailhouse/cells.  The
# suite stages a known-good copy next to this script; prefer it when it is
# there so the test survives a reflash.
if [ -f "$DIR/cells/$INMATE_CELL" ]; then
	INMATE_PATH="$DIR/cells/$INMATE_CELL"
else
	INMATE_PATH="$CELLS/$INMATE_CELL"
fi

cd "$DIR"

# Tear down a previous inmate before touching the Linux driver.  While a cell
# holds the AFE its registers are unmapped from the root cell, and a runtime
# resume against unmapped registers faults the root cell and resets the board.
if lsmod | grep -q jailhouse; then
	jailhouse cell shutdown zephyr 2>/dev/null || true
	jailhouse cell destroy  zephyr 2>/dev/null || true
fi

echo on > /sys/devices/platform/soc/10b10000.afe/power/control
grep -E "^audio " /sys/kernel/debug/pm_genpd/pm_genpd_summary

modprobe jailhouse 2>/dev/null || true

if ! jailhouse cell list 2>/dev/null | grep -qE "^[0-9]"; then
	jailhouse enable $CELLS/$ROOT_CELL
fi

jailhouse cell create "$INMATE_PATH"
jailhouse cell load   zephyr "$IMAGE" -a 0x00008000
jailhouse cell start  zephyr

jailhouse cell list
