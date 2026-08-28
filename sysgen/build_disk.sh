#!/usr/bin/env sh
# CP/M Neo OS build backend — driven by the sysgen tool.
#
#   sh sysgen/build_disk.sh --arch=<ARCH> --mem=<KB> --platform=<PLATFORM>
#
# Builds the bootloader, kernel and CCP into sysgen/build/, next to the
# tool binary.  Runs from anywhere: it locates the CP/M Neo root relative
# to its own path.  System and user apps are not built here — they are
# compiled by sysgen/app_build.sh and installed into the disk image by
# 'sysgen new' / 'sysgen install'.

set -eu

ARCH=""
MEM_SIZE=""
PLATFORM=""

for arg in "$@"; do
    case "$arg" in
        --arch=*)   ARCH="${arg#--arch=}" ;;
        --mem=*)    MEM_SIZE="${arg#--mem=}" ;;
        --platform=*) PLATFORM="${arg#--platform=}" ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

ARCH=${ARCH:-riscv32}
MEM_SIZE=${MEM_SIZE:?"--mem is required (e.g. --mem=64K)"}
PLATFORM=${PLATFORM:?"--platform is required"}

# Where the TPA (CCP/apps) is loaded and linked to run from. 0x0100 for
# platforms whose RAM starts at address 0 (e.g. vemu); TinyMCU's RAM
# starts at 0x02000000 (rtl/tinymcu_pkg.vhd's RAM_BASE), so 0x0100 alone
# would fall inside its read-only Boot ROM instead of RAM. Must match
# kernel_abi.h's TPA_LOAD_ADDR (platform/tinymcu/platform_base.h feeds
# that automatically via -I platform/$PLATFORM); passed to the CCP's
# linker step below via --defsym=__tpa_base so sdk/linker/linker_sdk.ld
# links it to run at the address the kernel will actually load it to.
case "$PLATFORM" in
    tinymcu) TPA_BASE=0x02000100 ;;
    *)       TPA_BASE=0x0100 ;;
esac

# Convert --mem=64K style suffix to hex bytes for ld --defsym
mem_kb=$(printf '%s' "$MEM_SIZE" | tr '[:lower:]' '[:upper:]')
case "$mem_kb" in
    *K) mem_kb=${mem_kb%K} ;;
    *)  echo "--mem value must have K suffix (e.g. --mem=64K)" >&2; exit 1 ;;
esac
MEM_HEX=$(printf '0x%X' "$((mem_kb * 1024))")

SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SELF/.." && pwd)
cd "$ROOT"

BUILD="$SELF/build"
INT="$BUILD/core/int"
K_OBJ="$BUILD/core/obj/kernel"
CCP_OBJ="$BUILD/core/obj/ccp"
SDK_OBJ="$BUILD/sdk/obj"
SDK_LIB="$BUILD/sdk/lib"

# Architecture metadata (toolchain prefix + CFLAGS) from arch/$ARCH/config.sh
# shellcheck source=/dev/null
. "arch/$ARCH/config.sh"

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

PLATFORM_INC="-I platform/$PLATFORM"
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
    -c "platform/$PLATFORM/bios.c" -o "$INT/boot_plat.o"
$CC $CFLAGS -I arch/$ARCH/ -I core/kernel/ \
    -Wl,--gc-sections -Wl,--strip-debug \
    -Wl,--defsym=__mem_size="$MEM_HEX" \
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
          core/kernel/disk.c platform/$PLATFORM/bios.c \
          sdk/src/string.c sdk/src/stdio.c sdk/src/fs.c sdk/src/stdlib.c"
KERNEL_S="sdk/src/entry.S"

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
    --defsym=__mem_size="$MEM_HEX" \
    -T core/kernel/linker_kernel.ld \
    $KERNEL_OBJS "$LIBGCC" -o "$INT/kernel_pass1.elf"

KERN_TOTAL_HEX=$($OBJDUMP -t "$INT/kernel_pass1.elf" | awk '/[[:space:]]__kernel_total$/{print "0x"$1}')
IO_BASE_HEX=$($OBJDUMP -t "$INT/kernel_pass1.elf" | awk '/[[:space:]]__io_base$/{print "0x"$1}')
KSTACK_GUARD_HEX=$($OBJDUMP -t "$INT/kernel_pass1.elf" | awk '/[[:space:]]__kstack_guard$/{print "0x"$1}')
KERN_TOTAL=$(printf "%d" "$KERN_TOTAL_HEX")
IO_BASE=$(printf "%d" "$IO_BASE_HEX")
KSTACK_GUARD=$(printf "%d" "$KSTACK_GUARD_HEX")
KERN_START=$(((IO_BASE - KERN_TOTAL - KSTACK_GUARD) & ~3))
KERN_START_HEX=0x$(printf '%x' "$KERN_START")

$LD $LDFLAGS \
    --defsym=__KERN_START="$KERN_START_HEX" \
    --defsym=__mem_size="$MEM_HEX" \
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
compile "$CFLAGS $SDK_INC" sdk/src/entry.S "$SDK_OBJ/entry.o"
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
    --defsym=__tpa_base="$TPA_BASE" \
    $CCP_OBJS "$SDK_OBJ/entry.o" "$LIBGCC" \
    --just-symbols="$INT/kernel.elf" -o "$INT/ccp.elf"
$OBJCOPY -O binary "$INT/ccp.elf" "$INT/ccp.bin"

printf '%s' "$ARCH" > "$BUILD/.arch"