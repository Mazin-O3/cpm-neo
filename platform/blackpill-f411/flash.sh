#!/usr/bin/env sh
# platform/blackpill-f411/flash.sh
# CP/M Neo — pack + flash BPF411.bin to a WeAct Black Pill STM32F411CEU6.
#
#   sh platform/blackpill-f411/flash.sh st [BUILD_DIR]
#       Pack the current bootloader.bin + disk.img into BPF411.bin, then
#       program it via ST-Link (SWD).  The ST-Link stays on the same board
#       regardless of BOOT0, and this is the reliable, verify-in-line path:
#         st-flash write BPF411.bin 0x08000000
#       No BOOT0 hold is required.  When it finishes, press NRST (RST) to
#       boot the new firmware.
#
# Packing happens here, in flash.sh, not in build.sh: disk.img is rebuilt
# from scratch by 'sysgen new', so a pack baked into build.sh would snapshot
# a disk before any 'sysgen install' — dropping those apps from the image.
# Assembling at flash time guarantees whatever disk.img currently holds
# (built-in apps plus any installed ones) is what lands on the chip.
#
# `st` mode prerequisites: st-flash from stlink-tools (sudo apt install
#                           stlink-tools).  The ST-Link must be connected
#                           and show up as 0483:3748 (or 374b) in lsusb.

set -eu

SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SELF/../.." && pwd)
BUILD_DIR=${2:-"$ROOT/sysgen/build"}
BOOT="$BUILD_DIR/bootloader.bin"
DISK="$BUILD_DIR/disk.img"
IMAGE="$BUILD_DIR/BPF411.bin"
FLASH_BASE=0x08000000
FLASH_TOP=$((0x08080000))   # end of 512 KB on-chip flash
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

# ── Assemble BPF411.bin from the current bootloader + disk ────────────────

[ -f "$BOOT" ] || { echo "flash.sh: $BOOT not found (run build.sh first)" >&2; exit 1; }
[ -f "$DISK" ] || { echo "flash.sh: $DISK not found (run build.sh first)" >&2; exit 1; }

# Boot size from the build tag (written by build_disk.sh from the linker's
# __boot_size symbol).  The XIP base is auto-derived the same way the build
# does: BOOT_BASE + boot size.
if [ -f "$BUILD_DIR/.boot_size" ]; then
    BOOT_SIZE=$(cat "$BUILD_DIR/.boot_size")
else
    echo "flash.sh: $BUILD_DIR/.boot_size not found (run build.sh first)" >&2
    exit 1
fi

BOOT_BYTES=$(wc -c < "$BOOT")
if [ "$BOOT_BYTES" -gt "$BOOT_SIZE" ]; then
    echo "flash.sh: bootloader.bin $BOOT_BYTES bytes > $BOOT_SIZE (__boot_size)" >&2
    exit 1
fi

DISK_BYTES=$(wc -c < "$DISK")
IMG_BYTES=$((BOOT_SIZE + DISK_BYTES))
IMG_TOP=$((FLASH_BASE + IMG_BYTES))

if [ "$IMG_TOP" -gt "$FLASH_TOP" ]; then
    echo "flash.sh: image $IMG_BYTES bytes tops out at 0x$(printf '%x' "$IMG_TOP") > flash end 0x$(printf '%x' "$FLASH_TOP")" >&2
    exit 1
fi

# The bootloader is 0xFF-padded to __boot_size (bulk-programmed flash) so the
# disk image lands exactly at the XIP base — bios.c reads sector n as
# flash[XIP_BASE + n*512] with XIP_BASE = BOOT_BASE + __boot_size.
PAD=$((BOOT_SIZE - BOOT_BYTES))
{
    cat "$BOOT"
    head -c "$PAD" /dev/zero | tr '\000' '\377'
    cat "$DISK"
} > "$IMAGE"

# Verify the disk image landed at the XIP base: S0_SIG (0xAA55) must sit at
# flash offset XIP_BASE + 0x1FE → file offset BOOT_SIZE + 0x1FE.
SIG_BYTES=$(dd if="$IMAGE" bs=1 skip=$((BOOT_SIZE + 0x1FE)) count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$SIG_BYTES" != "a a5" ] && [ "$SIG_BYTES" != "55aa" ]; then
    echo "flash.sh: WARNING: S0_SIG @0x$(printf '%x' $((FLASH_BASE + BOOT_SIZE + 0x1FE))) = $SIG_BYTES, expected 55aa" >&2
fi

# ── ST-Link prerequisites ────────────────────────────────────────────────

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

echo "flash.sh: $IMAGE = $IMG_BYTES bytes total (boot 0x08000000:$BOOT_SIZE + disk $DISK_BYTES)" >&2
echo "flash.sh: st-flash write $IMAGE $FLASH_BASE" >&2
if st-flash write "$IMAGE" "$FLASH_BASE"; then
    echo "flash.sh: done.  Press NRST (RST) to boot the new firmware." >&2
    exit 0
fi

echo "flash.sh: st-flash failed (check SWD wiring and board power)." >&2
exit 1