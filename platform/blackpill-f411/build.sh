#!/usr/bin/env sh
# platform/blackpill-f411/build.sh
# CP/M Neo — build the BPF411 firmware image: sysgen disk (always XIP) + pack.
#
#   sh platform/blackpill-f411/build.sh
#
# Blackpill always builds XIP: `sysgen new --xip` links the kernel/CCP in
# place (XIP base auto-derived as BOOT_BASE + boot size), then packs the
# image: the bootloader is 0xFF-padded to __boot_size and the disk image is
# appended, landing sector 0 exactly at the XIP base.  Produces
# <build>/BPF411.bin — the single image to flash with flash.sh.
#
# Flash plan (512 KB, XIP on the disk image):
#   0x08000000  bootloader            boot/base.  build_disk.sh asserts it
#                                   fits __boot_size (from linker); we pad
#                                   the shortfall with 0xFF to fill that window.
#   XIP base    disk.img               (auto-derived as BOOT_BASE + boot size).
#                                   The sysgen disk image (VMAP, kernel.bin,
#                                   CCP, apps) is stored verbatim so the
#                                   kernel runs XIP.
#
# Boot loader is 0xFF-padded to __boot_size so the disk image always lands
# exactly at the XIP base — bios.c reads sector n as flash[XIP_BASE + n*512].

set -eu

SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SELF/../.." && pwd)
BUILD_DIR="$ROOT/sysgen/build"
SYSGEN="$BUILD_DIR/sysgen"

if ! [ -x "$SYSGEN" ]; then
    echo "build.sh: $SYSGEN not found — run 'make -C sysgen' first" >&2
    exit 1
fi

"$SYSGEN" new --platform=BPF411 --xip

BOOT="$BUILD_DIR/bootloader.bin"
DISK="$BUILD_DIR/disk.img"
OUT="$BUILD_DIR/BPF411.bin"

# Read boot size from the build tag (written by build_disk.sh from the
# linker's __boot_size symbol).  The XIP base is auto-derived the same way
# the build does: BOOT_BASE + boot size.
BOOT_BASE=$((0x08000000))   # flash base = reset vector
if [ -f "$BUILD_DIR/.boot_size" ]; then
    BOOT_SIZE=$(cat "$BUILD_DIR/.boot_size")
else
    echo "build.sh: $BUILD_DIR/.boot_size not found (run sysgen new first)" >&2
    exit 1
fi
XIP_BASE=$((BOOT_BASE + BOOT_SIZE))
FLASH_TOP=$((0x08080000))   # end of 512 KB on-chip flash

[ -f "$BOOT" ] || { echo "build.sh: $BOOT not found (run sysgen new first)" >&2; exit 1; }
[ -f "$DISK" ] || { echo "build.sh: $DISK not found (run sysgen new first)" >&2; exit 1; }

BOOT_BYTES=$(wc -c < "$BOOT")
if [ "$BOOT_BYTES" -gt "$BOOT_SIZE" ]; then
    echo "build.sh: bootloader.bin $BOOT_BYTES bytes > $BOOT_SIZE (__boot_size)" >&2
    exit 1
fi

DISK_BYTES=$(wc -c < "$DISK")
IMG_BYTES=$((BOOT_SIZE + DISK_BYTES))
IMG_TOP=$((BOOT_BASE + IMG_BYTES))

if [ "$IMG_TOP" -gt "$FLASH_TOP" ]; then
    echo "build.sh: image $IMG_BYTES bytes tops out at 0x$(printf '%x' "$IMG_TOP") > flash end 0x$(printf '%x' "$FLASH_TOP")" >&2
    exit 1
fi

# Boot window is BOOT_BASE..BOOT_BASE+BOOT_SIZE; disk.img must start at XIP base.
if [ "$XIP_BASE" -ne "$((BOOT_BASE + BOOT_SIZE))" ]; then
    echo "build.sh: internal layout mismatch (boot end != XIP_BASE)" >&2
    exit 1
fi

# Pad the boot image to __boot_size with 0xFF (bulk-programmed flash), then
# append the disk image so sector 0 of disk.img sits at the XIP base.
PAD=$((BOOT_SIZE - BOOT_BYTES))
{
    cat "$BOOT"
    head -c "$PAD" /dev/zero | tr '\000' '\377'
    cat "$DISK"
} > "$OUT"

# Verify the disk image lands at the XIP base: S0_SIG (0xAA55) must sit at
# flash offset XIP_BASE + 0x1FE → file offset BOOT_SIZE + 0x1FE.
SIG_BYTES=$(dd if="$OUT" bs=1 skip=$((BOOT_SIZE + 0x1FE)) count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$SIG_BYTES" != "a a5" ] && [ "$SIG_BYTES" != "55aa" ]; then
    echo "build.sh: WARNING: S0_SIG @0x$(printf '%x' $((XIP_BASE + 0x1FE))) = $SIG_BYTES, expected 55aa" >&2
fi

echo "build.sh: $OUT = $IMG_BYTES bytes total (boot 0x08000000:$BOOT_SIZE + disk at 0x$(printf '%x' "$XIP_BASE"):$DISK_BYTES)" >&2
echo "build.sh: ok — flash an image top 0x$(printf '%x' "$IMG_TOP")" >&2
echo "build.sh: $BUILD_DIR/BPF411.bin ready — flash with 'sh platform/blackpill-f411/flash.sh'"