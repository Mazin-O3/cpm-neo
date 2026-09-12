#!/usr/bin/env sh
# platform/blackpill-f411/build.sh
# CP/M Neo — build the BPF411 disk image (always XIP).
#
#   sh platform/blackpill-f411/build.sh
#
# Blackpill always builds XIP: `sysgen new --xip` links the kernel/CCP in
# place (XIP base auto-derived as BOOT_BASE + boot size) and produces
# <build>/disk.img plus <build>/bootloader.bin.
#
# This script does NOT pack the flash image — that would stale-cache the
# disk (sysgen new rebuilds it from scratch, dropping apps installed on a
# previous image).  Use flash.sh, which assembles BPF411.bin from the
# current bootloader.bin + disk.img at flash time:
#
#   sh build.sh          # regenerate disk.img
#   sysgen install ./x.c # add far-away apps to disk.img (optional)
#   sh flash.sh          # pack + flash
#
# Flash plan (512 KB, XIP on the disk image):
#   0x08000000  bootloader            boot/base.  build_disk.sh asserts it
#                                   fits __boot_size (from linker); flash.sh
#                                   pads the shortfall with 0xFF.
#   XIP base    disk.img               (auto-derived as BOOT_BASE + boot size).
#                                   The sysgen disk image (VMAP, kernel.bin,
#                                   CCP, apps) is stored verbatim so the
#                                   kernel runs XIP.

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