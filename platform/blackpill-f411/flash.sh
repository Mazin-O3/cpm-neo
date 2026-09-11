#!/usr/bin/env sh
# platform/blackpill-f411/flash.sh
# CP/M Neo — flash BPF411.bin to a WeAct Black Pill STM32F411CEU6.
#
#   sh platform/blackpill-f411/flash.sh st [BUILD_DIR]
#       Program via ST-Link (SWD).  The ST-Link stays on the same board
#       regardless of BOOT0, and this is the reliable, verify-in-line path:
#         st-flash write BPF411.bin 0x08000000
#       No BOOT0 hold is required.  When it finishes, press NRST (RST) to
#       boot the new firmware.
#
# `st` mode prerequisites: st-flash from stlink-tools (sudo apt install
#                           stlink-tools).  The ST-Link must be connected
#                           and show up as 0483:3748 (or 374b) in lsusb.
#
# The image is assembled by build.sh (running from the platform dir) into
# <BUILD_DIR>/BPF411.bin, defaulting to sysgen/build.

set -eu

SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SELF/../.." && pwd)
BUILD_DIR=${2:-"$ROOT/sysgen/build"}
IMAGE="$BUILD_DIR/BPF411.bin"
FLASH_BASE=0x08000000
STLINK_VID_PID_1=0483:3748
STLINK_VID_PID_2=0483:374b

PROG=${1:-st}
case "$PROG" in
    st) ;;
    *)
        echo "flash.sh: usage: sh platform/blackpill-f411/flash.sh [st] [BUILD_DIR]" >&2
        echo "           st — ST-Link (SWD), reliable, no BOOT0 needed" >&2
        exit 1
        ;;
esac

if ! [ -f "$IMAGE" ]; then
    echo "flash.sh: $IMAGE not found (run build.sh first)" >&2
    exit 1
fi

if ! command -v st-flash >/dev/null 2>&1; then
    echo "flash.sh: st-flash not found — install stlink-tools:" >&2
    echo "          sudo apt install stlink-tools" >&2
    exit 1
fi

if command -v lsusb >/dev/null 2>&1; then
    if ! lsusb -d "$STLINK_VID_PID_1" 2>/dev/null | grep -q . \
       && ! lsusb -d "$STLINK_VID_PID_2" 2>/dev/null | grep -q .; then
        echo "flash.sh: no ST-Link ($STLINK_VID_PID_1/$STLINK_VID_PID_2) on the USB bus." >&2
        echo "          Connect the ST-Link SWD probe, then run this script again." >&2
        exit 1
    fi
fi

echo "flash.sh: st-flash write $IMAGE $FLASH_BASE" >&2
if st-flash write "$IMAGE" "$FLASH_BASE"; then
    echo "flash.sh: done.  Press NRST (RST) to boot the new firmware." >&2
    exit 0
fi

echo "flash.sh: st-flash failed (check SWD wiring and board power)." >&2
exit 1