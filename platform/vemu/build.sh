#!/usr/bin/env bash
set -euo pipefail

# Navigate to the workspace root directory 
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."

# Define paths relative to the workspace root
SYSGEN_BUILD="cpm-neo/sysgen/build"
SYSGEN="$SYSGEN_BUILD/sysgen"

DEST_DIR="vemu/cpm-neo"
APPS_DIR="vemu/apps"
MISC_DIR="vemu/misc"

# Build + install the same working set into a fresh image, then copy the
# produced artifacts out.  Run once per mode so vemu ships BOTH disk flavors:
#   disk.img        — S0_XIP=0, non-XIP (RAM-loaded kernel/CCP)
#   disk-xip.img    — S0_XIP=1, XIP (kernel/CCP run in place from flash)
#   bootloader.bin  — shared by both images (built with XIP_BASE; its RAM
#                     vs XIP boot path is picked at runtime from S0_XIP).
build_and_install() {
    "$SYSGEN" new --platform=vemu "$@"

    for app in snake.c mbrot.c; do
        "$SYSGEN" install "$APPS_DIR/$app"
    done

    "$SYSGEN" add "$MISC_DIR"
}

echo "== stage 1: non-XIP disk =="
build_and_install
cp "$SYSGEN_BUILD/disk.img" "$DEST_DIR/disk.img"

echo "== stage 2: XIP disk + shared bootloader =="
build_and_install --xip
cp "$SYSGEN_BUILD/disk.img"       "$DEST_DIR/disk-xip.img"
cp "$SYSGEN_BUILD/bootloader.bin" "$DEST_DIR/bootloader.bin"

echo "Build: staged disk.img + disk-xip.img + bootloader.bin into $DEST_DIR"