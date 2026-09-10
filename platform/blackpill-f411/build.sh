#!/usr/bin/env sh
# platform/blackpill-f411/build.sh
# CP/M Neo — build the BPF411 firmware image: sysgen disk (always XIP) + pack.
#
#   sh platform/blackpill-f411/build.sh
#
# Blackpill always builds XIP: `sysgen new --xip` links the kernel/CCP in
# place (XIP base auto-derived as BOOT_BASE + boot size), then pack.sh pads
# the bootloader and appends the disk image.  Produces <build>/BPF411.bin —
# the single image to flash with flash.sh.

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
sh "$ROOT/platform/blackpill-f411/pack.sh" "$BUILD_DIR"

echo "build.sh: $BUILD_DIR/BPF411.bin ready — flash with 'sh platform/blackpill-f411/flash.sh'"