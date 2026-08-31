#!/usr/bin/env sh
# CP/M Neo OS build backend — driven by the sysgen tool.
#
#   sh sysgen/build_disk.sh --platform=<PLATFORM>
#
# Builds the bootloader, kernel and CCP into sysgen/build/, next to the
# tool binary.  Runs from anywhere: it locates the CP/M Neo root relative
# to its own path.  System and user apps are not built here — they are
# compiled by sysgen/app_build.sh and installed into the disk image by
# 'sysgen new' / 'sysgen install'.
#
# The target's memory layout comes from platform/<PLATFORM>/config.sh
# (ID, ARCH, RAM_SIZE, IO_BASE, RAM_BASE).  Everything else is derived here:
#   RAM_END   = RAM_BASE + RAM_SIZE   (nominal end of the SRAM region)
#   RAM_TOP   = min(RAM_END, IO_BASE) (top of usable RAM; what the kernel
#                                      packs below — __ram_top)
#   TPA_BASE  = RAM_BASE + 0x100      (CP/M TPA load address)

set -eu

PLATFORM_ID=""

for arg in "$@"; do
    case "$arg" in
        --platform=*) PLATFORM_ID="${arg#--platform=}" ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

PLATFORM_ID=${PLATFORM_ID:?"--platform=<ID> is required"}

SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SELF/.." && pwd)
cd "$ROOT"

BUILD="$SELF/build"
INT="$BUILD/core/int"
K_OBJ="$BUILD/core/obj/kernel"
CCP_OBJ="$BUILD/core/obj/ccp"
SDK_OBJ="$BUILD/sdk/obj"
SDK_LIB="$BUILD/sdk/lib"

# ---------------------------------------------------------------------------
# Platform lookup: --platform=<ID> must match the ID= field of one platform
# config.sh.  The platform folder is purely a filesystem location derived
# here; it is never a platform identity.
# ---------------------------------------------------------------------------
match_id() {
    awk -F= '
        /^[[:space:]]*ID=/ {
            v=$2
            gsub(/[ \t\r]/, "", v)
            gsub(/^"+|"+$/, "", v)
            if (v != "" && !done) { print v; done=1 }
        }' "$1"
}

PLATFORM_DIR=""
for CFG in platform/*/config.sh; do
    CFG_ID=$(match_id "$CFG")

    if [ -z "$CFG_ID" ]; then
        continue
    fi

    CFG_ID_U=$(printf '%s' "$CFG_ID"            | tr '[:lower:]' '[:upper:]')
    ARG_ID_U=$(printf '%s' "$PLATFORM_ID"       | tr '[:lower:]' '[:upper:]')

    if [ "$CFG_ID_U" = "$ARG_ID_U" ]; then
        if [ -n "$PLATFORM_DIR" ]; then
            OTHER_DIR=${CFG%/config.sh}
            OTHER_DIR=${OTHER_DIR#platform/}

            echo "ERROR: duplicate platform ID '$PLATFORM_ID' in '$PLATFORM_DIR' and '$OTHER_DIR'" >&2
            exit 1
        fi

        PLATFORM_DIR=${CFG%/config.sh}
        PLATFORM_DIR=${PLATFORM_DIR#platform/}
    fi
done

if [ -z "$PLATFORM_DIR" ]; then
    echo "ERROR: unknown platform '$PLATFORM_ID'" >&2
    exit 1
fi

# Platform metadata (ID, ARCH, RAM_SIZE, IO_BASE, RAM_BASE) from platform/$PLATFORM_DIR/config.sh
# shellcheck source=/dev/null
. "platform/$PLATFORM_DIR/config.sh"

ARCH=${ARCH:?"$PLATFORM_ID: ARCH not set in platform/$PLATFORM_DIR/config.sh"}
IO_BASE=${IO_BASE:?"$PLATFORM_ID: IO_BASE not set in platform/$PLATFORM_DIR/config.sh"}
RAM_BASE=${RAM_BASE:?"$PLATFORM_ID: RAM_BASE not set in platform/$PLATFORM_DIR/config.sh"}
RAM_SIZE=${RAM_SIZE:?"$PLATFORM_ID: RAM_SIZE not set in platform/$PLATFORM_DIR/config.sh"}
ID=${ID:?"$PLATFORM_ID: ID not set in platform/$PLATFORM_DIR/config.sh (8-char OS platform id)"}

CFG_ID_U=$(printf '%s' "$ID"          | tr '[:lower:]' '[:upper:]')
ARG_ID_U=$(printf '%s' "$PLATFORM_ID" | tr '[:lower:]' '[:upper:]')
if [ "$CFG_ID_U" != "$ARG_ID_U" ]; then
    echo "ERROR: platform ID mismatch: config.sh declares '$ID' but --platform=$PLATFORM_ID" >&2
    exit 1
fi

if [ "${#ID}" -gt 8 ]; then
    echo "ERROR: ID '$ID' exceeds the 8-char S0_PLATFORM limit" >&2
    exit 1
fi

# Architecture metadata (toolchain prefix + CFLAGS) from arch/$ARCH/config.sh
# shellcheck source=/dev/null
. "arch/$ARCH/config.sh"

# Derived layout.  RAM_END is the nominal end of SRAM (RAM_BASE + RAM_SIZE);
# RAM_TOP is the top of usable RAM and may be lower when an MMIO window lies
# inside the nominal RAM range (as on vemu): RAM_TOP = min(RAM_END, IO_BASE).
# The clamp keeps the kernel from ever colliding with that window.  On a real
# MCU where peripherals are mapped far above SRAM, IO_BASE > RAM_END and
# RAM_TOP falls back to RAM_END — the whole SRAM region is usable.  The linker
# scripts below enforce the real invariants: boot scratch/stack, the kernel
# image, and the CCP/TPA must all fit under __ram_top — a clashing IO_BASE or
# RAM_BASE therefore fails the link, never producing a broken image.
RAM_BASE_DEC=$((RAM_BASE))
RAM_END_DEC=$((RAM_BASE + RAM_SIZE))
IO_BASE_DEC=$((IO_BASE))
if [ "$RAM_END_DEC" -lt "$IO_BASE_DEC" ]; then
    RAM_TOP_DEC=$RAM_END_DEC
else
    RAM_TOP_DEC=$IO_BASE_DEC
fi
TPA_BASE_DEC=$((RAM_BASE + 0x100))
IO_BASE_HEX=$(printf '0x%X' "$IO_BASE_DEC")
RAM_TOP_HEX=$(printf '0x%X' "$RAM_TOP_DEC")
TPA_BASE_HEX=$(printf '0x%X' "$TPA_BASE_DEC")

CC=${CROSS_COMPILE}gcc
LD=${CROSS_COMPILE}ld
OBJCOPY=${CROSS_COMPILE}objcopy
OBJDUMP=${CROSS_COMPILE}objdump
AR=${CROSS_COMPILE}ar

ARCH_FLAGS="$ARCH_CFLAGS"
LIBGCC=$($CC $ARCH_FLAGS -print-libgcc-file-name)

CFLAGS="$ARCH_FLAGS -ffreestanding -nostdlib \
        -Os -ffunction-sections -fdata-sections \
        -fno-builtin -fomit-frame-pointer \
        -Wall -Wextra"
LDFLAGS="--gc-sections --strip-debug --no-warn-rwx-segments -m $LD_EMULATION"

PLATFORM_INC="-I platform/$PLATFORM_DIR"
KERNEL_INC="-I core/kernel/ -I sdk/include -I core/ -I ./ $PLATFORM_INC"
CCP_INC="-I core/ccp/ -I core/kernel/ -I sdk/include -I core/ -I ./ $PLATFORM_INC"
SDK_INC="-I sdk/include -I core/kernel/ -I core/ -I ./ $PLATFORM_INC"

compile() {
    mkdir -p "$(dirname "$3")"
    $CC $1 -c "$2" -o "$3"
}

mkdir -p "$BUILD" "$INT" "$SDK_LIB"

# ── Bootloader ─────────────────────────────────────────────
echo "  Building bootloader..."
$CC $CFLAGS $PLATFORM_INC -I core/kernel/ \
    -c "platform/$PLATFORM_DIR/bios.c" -o "$INT/boot_plat.o"
$CC $CFLAGS -I arch/$ARCH/ -I core/kernel/ \
    -Wl,--gc-sections -Wl,--strip-debug -Wl,--no-warn-rwx-segments \
    -Wl,--defsym=__io_base="$IO_BASE_HEX" \
    -Wl,--defsym=__ram_top="$RAM_TOP_HEX" \
    -T arch/$ARCH/linker_boot.ld \
    arch/$ARCH/boot.S "$INT/boot_plat.o" -o "$INT/bootloader.elf"
$OBJCOPY -O binary --only-section=.boot "$INT/bootloader.elf" "$BUILD/bootloader.bin"
SIZE=$(wc -c < "$BUILD/bootloader.bin")
if [ "$SIZE" -gt 1024 ]; then
    echo "ERROR: bootloader.bin $SIZE bytes > 1024" >&2
    exit 1
fi

# ── Kernel (two-pass) ──────────────────────────────────────
echo "  Building kernel..."
KERNEL_C="core/kernel/main.c core/kernel/kernel.c core/kernel/bdos.c \
          core/kernel/disk.c platform/$PLATFORM_DIR/bios.c \
          sdk/src/string.c sdk/src/stdio.c sdk/src/fs.c sdk/src/stdlib.c"
KERNEL_S="arch/$ARCH/crt0.S"

KERNEL_OBJS=
for src in $KERNEL_C; do
    obj="$K_OBJ/${src%.c}.o"
    compile "$CFLAGS $KERNEL_INC" "$src" "$obj"
    KERNEL_OBJS="$KERNEL_OBJS $obj"
done
for src in $KERNEL_S; do
    obj="$K_OBJ/${src%.S}.o"
    compile "$CFLAGS $KERNEL_INC" "$src" "$obj"
    KERNEL_OBJS="$KERNEL_OBJS $obj"
done

$LD $LDFLAGS \
    --defsym=__KERN_START=0x4000 \
    --defsym=__io_base="$IO_BASE_HEX" \
    --defsym=__ram_top="$RAM_TOP_HEX" \
    --defsym=__tpa_base="$TPA_BASE_HEX" \
    -T core/kernel/linker_kernel.ld \
    $KERNEL_OBJS "$LIBGCC" -o "$INT/kernel_pass1.elf"

KERN_TOTAL_HEX=$($OBJDUMP -t "$INT/kernel_pass1.elf" | awk '/[[:space:]]__kernel_total$/{print "0x"$1}')
KSTACK_GUARD_HEX=$($OBJDUMP -t "$INT/kernel_pass1.elf" | awk '/[[:space:]]__kstack_guard$/{print "0x"$1}')
KERN_TOTAL=$(printf "%d" "$KERN_TOTAL_HEX")
KSTACK_GUARD=$(printf "%d" "$KSTACK_GUARD_HEX")
KERN_START=$(((RAM_TOP_DEC - KERN_TOTAL - KSTACK_GUARD) & ~3))
KERN_START_HEX=0x$(printf '%x' "$KERN_START")

$LD $LDFLAGS \
    --defsym=__KERN_START="$KERN_START_HEX" \
    --defsym=__io_base="$IO_BASE_HEX" \
    --defsym=__ram_top="$RAM_TOP_HEX" \
    --defsym=__tpa_base="$TPA_BASE_HEX" \
    -T core/kernel/linker_kernel.ld \
    $KERNEL_OBJS "$LIBGCC" -o "$INT/kernel.elf"

BSS_END=$($OBJDUMP -t "$INT/kernel.elf" | awk '/[[:space:]]_bss_end$/{print "0x"$1}')
KSTACK=$($OBJDUMP -t "$INT/kernel.elf" | awk '/[[:space:]]__kstack_origin$/{print "0x"$1}')
if [ -n "$BSS_END" ] && [ -n "$KSTACK" ]; then
    BSS_DEC=$(printf "%d" "$BSS_END")
    STK_DEC=$(printf "%d" "$KSTACK")
    if [ "$BSS_DEC" -gt "$STK_DEC" ]; then
        echo "ERROR: Kernel .bss overlaps the stack!" >&2
        exit 1
    fi
fi
$OBJCOPY -O binary "$INT/kernel.elf" "$INT/kernel.bin"

# ── SDK libc ───────────────────────────────────────────────
echo "  Building SDK libc..."
SDK_LIBC_SRCS="sdk/src/stdio.c sdk/src/string.c sdk/src/stdlib.c sdk/src/fs.c sdk/src/ccplib.c sdk/src/start.c"
SDK_LIBC_OBJS=
for src in $SDK_LIBC_SRCS; do
    obj="$SDK_OBJ/$(basename "$src" .c).o"
    compile "$CFLAGS $SDK_INC" "$src" "$obj"
    SDK_LIBC_OBJS="$SDK_LIBC_OBJS $obj"
done
compile "$CFLAGS $SDK_INC" arch/$ARCH/crt0.S "$SDK_OBJ/crt0.o"
$AR rcs "$SDK_LIB/libc.a" $SDK_LIBC_OBJS

# ── CCP ───────────────────────────────────────────────────
echo "  Building CCP..."
CCP_C="sdk/src/start.c sdk/src/ccplib.c \
       core/ccp/ccp.c core/ccp/cmd_files.c core/ccp/cmd_system.c \
       sdk/src/string.c sdk/src/stdio.c sdk/src/fs.c sdk/src/stdlib.c"
CCP_OBJS=
for src in $CCP_C; do
    obj="$CCP_OBJ/${src%.c}.o"
    compile "$CFLAGS $CCP_INC" "$src" "$obj"
    CCP_OBJS="$CCP_OBJS $obj"
done
$LD $LDFLAGS -T sdk/linker/linker_sdk.ld \
    $CCP_OBJS "$SDK_OBJ/crt0.o" "$LIBGCC" \
    --just-symbols="$INT/kernel.elf" -o "$INT/ccp.elf"
$OBJCOPY -O binary "$INT/ccp.elf" "$INT/ccp.bin"

printf '%s' "$PLATFORM_DIR" > "$BUILD/.platform_dir"
printf '%s' "$ID" > "$BUILD/.platform_id"
printf '%s' "$ARCH" > "$BUILD/.arch"
printf '%s' "$ARCH_CFLAGS" > "$BUILD/.archflags"