#!/usr/bin/env sh
# CP/M Neo generic app builder — compiles any source folder to a .com.
#
#   sh sysgen/app_build.sh <PLATFORM_DIR> <APP_DIR> [-o OUT.com]
#
# <PLATFORM_DIR> is the internal platform directory (from sysgen/build/
# .platform_dir), used only to locate the platform's config.sh and includes.
#
# Scans <APP_DIR> recursively for .c/.s/.S sources and links a raw .com
# binary ready to run from the TPA.  User programs always load into the TPA
# and run there — only the kernel and CCP execute in place from flash on XIP
# disks — so every .com is linked with the standard TPA script regardless of
# the platform's CONFIG_XIP_BASE.  Requires a prior 'sysgen new' build
# (sdk/lib/libc.a, sdk/obj/crt0.o, core/int/kernel.elf).  Object files
# go under build/apps/obj/<appname>/ mirroring the source layout so
# multi-file apps with repeated filenames never collide.  Runs from
# anywhere: it locates the CP/M Neo root relative to its own path.

set -eu

PLATFORM_DIR=${1:?}
APP_DIR=${2:?}
OUT=
shift 2
while [ $# -gt 0 ]; do
    case "$1" in
        -o)
            OUT=${2:?}
            shift 2
            ;;
        *)
            echo "ERROR: unknown argument '$1'" >&2
            exit 1
            ;;
    esac
done

# Resolve a relative APP_DIR against the caller's working directory
# before we cd into the repo root below.
case "$APP_DIR" in
    /*) ;;
    *)
        if [ -d "$APP_DIR" ]; then
            APP_DIR=$(CDPATH= cd -- "$APP_DIR" && pwd)
        else
            APP_DIR=$(CDPATH= cd -- "$(dirname -- "$APP_DIR")" && pwd)/$(basename -- "$APP_DIR")
        fi
        ;;
esac

CALLER_CWD=$PWD

SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SELF/.." && pwd)
cd "$ROOT"

BUILD="$SELF/build"
INT="$BUILD/core/int"
SDK_OBJ="$BUILD/sdk/obj"
SDK_LIB="$BUILD/sdk/lib"

# Platform metadata (CONFIG_ARCH, CONFIG_IO_BASE, CONFIG_RAM_BASE, optional
# CONFIG_XIP_BASE) from platform/$PLATFORM_DIR/config.sh — CONFIG_XIP_BASE
# is informational here; .com files are never linked against the XIP region.
# shellcheck source=/dev/null
. "platform/$PLATFORM_DIR/config.sh"

CONFIG_ARCH=${CONFIG_ARCH:?"$PLATFORM_DIR: CONFIG_ARCH not set in platform/$PLATFORM_DIR/config.sh"}

# Architecture metadata (toolchain prefix + CFLAGS) from arch/$CONFIG_ARCH/config.sh
# shellcheck source=/dev/null
. "arch/$CONFIG_ARCH/config.sh"
CONFIG_CROSS_COMPILE=${CONFIG_CROSS_COMPILE:?"$CONFIG_ARCH: CONFIG_CROSS_COMPILE not set in arch/$CONFIG_ARCH/config.sh"}

CC=${CONFIG_CROSS_COMPILE}gcc
LD=${CONFIG_CROSS_COMPILE}ld
OBJCOPY=${CONFIG_CROSS_COMPILE}objcopy

SDK_LD=sdk/linker/linker_app.ld

ARCH_FLAGS="$CONFIG_ARCH_CFLAGS"
LIBGCC=$($CC $ARCH_FLAGS -print-libgcc-file-name)

CFLAGS="$ARCH_FLAGS -ffreestanding -nostdlib \
        -Os -ffunction-sections -fdata-sections \
        -fno-builtin -fomit-frame-pointer \
        -Wall -Wextra"
LDFLAGS="--gc-sections --strip-debug --no-warn-rwx-segments -m $CONFIG_LD_EMULATION"

# Effective platform config from the last build (build/gen/config.h) comes
# first, so user .com compiles see the same CONFIG_* values as the kernel.
SDK_INC="-I $BUILD/gen -I sdk/include -I core/kernel/ -I core/ -I ./"

APP_NAME=$(basename "$APP_DIR")

if [ ! -f "$SDK_LIB/libc.a" ] || [ ! -f "$SDK_OBJ/crt0.o" ] || [ ! -f "$INT/kernel.elf" ]; then
    echo "ERROR: no system build found in $BUILD" >&2
    echo "       run 'sysgen new' first (it builds the SDK and kernel)." >&2
    exit 1
fi

# Single-file mode: APP_DIR is a regular source file (mycmd.c).  The app
# name is the basename minus its extension, so mycmd.c -> mycmd.com.
if [ -f "$APP_DIR" ]; then
    APP_NAME=${APP_NAME%.*}
    INC="-I $(dirname "$APP_DIR")"
    SRCS="$APP_DIR"
else
    INC="-I $APP_DIR"
    for d in $(find "$APP_DIR" -type d); do
        INC="$INC -I $d"
    done
    SRCS=$(find "$APP_DIR" -type f \( -name '*.c' -o -name '*.s' -o -name '*.S' \) -print | sort)
fi

if [ -z "$OUT" ]; then
    OUT="$BUILD/apps/com/$APP_NAME.com"
fi

# Resolve a relative OUT against the caller's working directory, just like
# APP_DIR above, since we are now inside the repo root.
case "$OUT" in
    /*) ;;
    *) OUT="$CALLER_CWD/$OUT" ;;
esac

APP_OBJ="$BUILD/apps/obj/$APP_NAME"
mkdir -p "$APP_OBJ" "$(dirname "$OUT")"

objs=
for src in $SRCS; do
    if [ -f "$APP_DIR" ]; then
        rel=$(basename "$src")
    else
        rel=${src#"$APP_DIR"/}
        rel=${rel%.c}
        rel=${rel%.s}
        rel=${rel%.S}
    fi
    obj="$APP_OBJ/$rel.o"
    mkdir -p "$(dirname "$obj")"
    $CC $CFLAGS $SDK_INC $INC -c "$src" -o "$obj"
    objs="$objs $obj"
done

if [ -z "$objs" ]; then
    echo "ERROR: no .c/.s/.S sources found under $APP_DIR" >&2
    exit 1
fi

$LD $LDFLAGS -T $SDK_LD \
    $objs "$SDK_OBJ/crt0.o" "$SDK_LIB/libc.a" "$LIBGCC" \
    --just-symbols="$INT/kernel.elf" -o "$APP_OBJ/$APP_NAME.elf"
$OBJCOPY -O binary "$APP_OBJ/$APP_NAME.elf" "$OUT"
