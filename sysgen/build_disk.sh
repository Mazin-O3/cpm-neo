#!/usr/bin/env sh
# CP/M Neo OS build backend — driven by the sysgen tool.
#
#   sh sysgen/build_disk.sh --platform=<PLATFORM>
#
# XIP is requested explicitly with --xip on the command line: the kernel/CCP
# are linked into the flash window; a platform that declares CONFIG_XIP_BASE
# pins the window origin, otherwise it is auto-derived as BOOT_BASE + boot
# size.  Without --xip builds are non-XIP regardless of CONFIG_XIP_BASE.
#
# Builds the bootloader, kernel and CCP into sysgen/build/, next to the
# tool binary.  Runs from anywhere: it locates the CP/M Neo root relative
# to its own path.  System and user apps are not built here — they are
# compiled by sysgen/app_build.sh and installed into the disk image by
# 'sysgen new' / 'sysgen install'.
#
# The target's configuration comes from platform/<PLATFORM>/config.sh — the
# hardware facts (CONFIG_ID, CONFIG_ARCH, CONFIG_RAM_SIZE, CONFIG_IO_BASE,
# CONFIG_RAM_BASE, CONFIG_BOOT_BASE) and the four software knobs
# (CONFIG_VOL_MAX,
# CONFIG_DISK_SIZE, CONFIG_FCB_MAX, CONFIG_STACK_SIZE), all required (there
# are no defaults).  CONFIG_BOOT_SIZE and CONFIG_BOOT_RAM_SIZE are optional:
# the linker script provides defaults via PROVIDE(); a platform that needs a
# different budget overrides them.  The effective values are written to
# build/gen/config.h, which every kernel/CCP/SDK/app compile includes (and
# therefore every user .com build).
# Two OPTIONAL app-selection knobs pick the bundled apps 'sysgen new'
# installs on the disk: CONFIG_SYS_APPS lists apps/sys commands and
# CONFIG_EXTRA_APPS lists apps/extra apps.  For both, an unset or "*"
# value means install ALL of them, an empty "" means NONE, and a
# space-separated list filters down to those apps.  The resolved values
# are stamped into the build/.sys_apps and build/.extra_apps tags for
# sysgen to read back.
# Everything else is derived here:
#   RAM_END   = RAM_BASE + RAM_SIZE   (nominal end of the SRAM region)
#   RAM_TOP   = min(RAM_END, IO_BASE) (top of usable RAM; what the kernel
#                                      packs below — __ram_top)
#   TPA_BASE  = RAM_BASE + 0x100      (CP/M TPA load address)

set -eu

PLATFORM_ID=""
WANT_XIP=0

for arg in "$@"; do
    case "$arg" in
        --platform=*) PLATFORM_ID="${arg#--platform=}" ;;
        --xip) WANT_XIP=1 ;;
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
# Platform lookup: --platform=<ID> must match the CONFIG_ID= field of one
# platform config.sh.  The platform folder is purely a filesystem location
# derived here; it is never a platform identity.
# ---------------------------------------------------------------------------
match_id() {
    awk -F= '
        /^[[:space:]]*CONFIG_ID=/ {
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

# Platform metadata (CONFIG_ID, CONFIG_ARCH, CONFIG_RAM_SIZE, CONFIG_IO_BASE,
# CONFIG_RAM_BASE) from platform/$PLATFORM_DIR/config.sh
# shellcheck source=/dev/null
. "platform/$PLATFORM_DIR/config.sh"

CONFIG_ARCH=${CONFIG_ARCH:?"$PLATFORM_ID: CONFIG_ARCH not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_IO_BASE=${CONFIG_IO_BASE:?"$PLATFORM_ID: CONFIG_IO_BASE not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_RAM_BASE=${CONFIG_RAM_BASE:?"$PLATFORM_ID: CONFIG_RAM_BASE not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_RAM_SIZE=${CONFIG_RAM_SIZE:?"$PLATFORM_ID: CONFIG_RAM_SIZE not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_ID=${CONFIG_ID:?"$PLATFORM_ID: CONFIG_ID not set in platform/$PLATFORM_DIR/config.sh (8-char OS platform id)"}

# The four software knobs are required: each platform declares them in its
# config.sh (there are no defaults — a knob left out here is a build error,
# like the hardware fields above).
CONFIG_VOL_MAX=${CONFIG_VOL_MAX:?"$PLATFORM_ID: CONFIG_VOL_MAX not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_DISK_SIZE=${CONFIG_DISK_SIZE:?"$PLATFORM_ID: CONFIG_DISK_SIZE not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_FCB_MAX=${CONFIG_FCB_MAX:?"$PLATFORM_ID: CONFIG_FCB_MAX not set in platform/$PLATFORM_DIR/config.sh"}
CONFIG_STACK_SIZE=${CONFIG_STACK_SIZE:?"$PLATFORM_ID: CONFIG_STACK_SIZE not set in platform/$PLATFORM_DIR/config.sh"}

# Boot base is a platform memory-map constant (the flash/reset vector origin).
# Boot size and boot RAM size default to arch-owned values defined via PROVIDE
# in linker_boot.ld; a platform may override them by setting CONFIG_BOOT_SIZE /
# CONFIG_BOOT_RAM_SIZE in its config.sh.
CONFIG_BOOT_BASE=${CONFIG_BOOT_BASE:?"$PLATFORM_ID: CONFIG_BOOT_BASE not set in platform/$PLATFORM_DIR/config.sh"}

# One bitmap byte covers 8 blocks (8 KB); its size rounds up to the next
# whole byte (ceil(CONFIG_DISK_SIZE/8)), so any CONFIG_DISK_SIZE is valid.
if [ "$CONFIG_DISK_SIZE" -lt 8 ]; then
    echo "ERROR: CONFIG_DISK_SIZE=$CONFIG_DISK_SIZE is too small (minimum 8 KB)" >&2
    exit 1
fi

# Effective config header.  Kernel/CCP/SDK/app compiles include this (see
# PLATFORM_INC/_INCLUDE lists below), so every source sees the platform's
# effective values from one generated header — there is no core/config.h.
GEN_INC="-I $BUILD/gen"
mkdir -p "$BUILD/gen"
cat > "$BUILD/gen/config.h" <<EOF
#ifndef CONFIG_H
#define CONFIG_H
#define CONFIG_VOL_MAX        $CONFIG_VOL_MAX
#define CONFIG_DISK_SIZE      $CONFIG_DISK_SIZE
#define CONFIG_FCB_MAX        $CONFIG_FCB_MAX
#define CONFIG_STACK_SIZE     $CONFIG_STACK_SIZE
#endif /* CONFIG_H */
EOF

# XIP is requested explicitly with --xip: the kernel and CCP are linked into
# the XIP region at the XIP base (.data/.bss still live in RAM, and user .com
# files are always RAM-loaded from the TPA).  The platform may declare
# CONFIG_XIP_BASE as the window origin; when omitted, the XIP base is
# auto-derived as CONFIG_BOOT_BASE + boot size, resolved after the bootloader
# link.  Without --xip, XIP is off regardless of whether the platform declares
# CONFIG_XIP_BASE.  There is no configured XIP window size: the window starts
# at the XIP base and extends over the produced disk image, so sysgen sizes
# everything against the actual linked contents.
if [ "$WANT_XIP" = "1" ]; then
    IS_XIP=1
    KERN_LD="core/kernel/linker_kernel_xip.ld"
    SDK_LD="core/ccp/linker_ccp_xip.ld"
    XIP_BASE=${CONFIG_XIP_BASE:-}
else
    IS_XIP=0
    XIP_BASE=0
    KERN_LD="core/kernel/linker_kernel.ld"
    SDK_LD="sdk/linker/linker_app.ld"
fi

# Sector I/O byte count and kernel start sector come from the single on-disk
# format header (core/kernel/disk_format.h), which boot.S, the kernel, sysgen,
# and user programs all read.  These feed the XIP geometry resolved after the
# bootloader link below.
DISK_SECTOR_SIZE=$(awk '/^#define[[:space:]]+DISK_SECTOR_SIZE/{print $3; exit}' \
    core/kernel/disk_format.h)
DISK_SECTOR_SIZE=${DISK_SECTOR_SIZE:-512}
KERN_START_SEC=2       # core/kernel/disk_format.h: KERN_START_SEC (boot+VMAP)

CFG_ID_U=$(printf '%s' "$CONFIG_ID"  | tr '[:lower:]' '[:upper:]')
ARG_ID_U=$(printf '%s' "$PLATFORM_ID" | tr '[:lower:]' '[:upper:]')
if [ "$CFG_ID_U" != "$ARG_ID_U" ]; then
    echo "ERROR: platform ID mismatch: config.sh declares '$CONFIG_ID' but --platform=$PLATFORM_ID" >&2
    exit 1
fi

if [ "${#CONFIG_ID}" -gt 8 ]; then
    echo "ERROR: CONFIG_ID '$CONFIG_ID' exceeds the 8-char S0_PLATFORM limit" >&2
    exit 1
fi

# Architecture metadata (toolchain prefix + CFLAGS) from arch/$CONFIG_ARCH/config.sh
# shellcheck source=/dev/null
. "arch/$CONFIG_ARCH/config.sh"
CONFIG_CROSS_COMPILE=${CONFIG_CROSS_COMPILE:?"$CONFIG_ARCH: CONFIG_CROSS_COMPILE not set in arch/$CONFIG_ARCH/config.sh"}
CONFIG_ARCH_CFLAGS=${CONFIG_ARCH_CFLAGS:?"$CONFIG_ARCH: CONFIG_ARCH_CFLAGS not set in arch/$CONFIG_ARCH/config.sh"}
CONFIG_LD_EMULATION=${CONFIG_LD_EMULATION:?"$CONFIG_ARCH: CONFIG_LD_EMULATION not set in arch/$CONFIG_ARCH/config.sh"}

# Derived layout.  RAM_END is the nominal end of SRAM (RAM_BASE + RAM_SIZE);
# RAM_TOP is the top of usable RAM and may be lower when an MMIO window lies
# inside the nominal RAM range (as on vemu): RAM_TOP = min(RAM_END, IO_BASE).
# The clamp keeps the kernel from ever colliding with that window.  On a real
# MCU where peripherals are mapped far above SRAM, IO_BASE > RAM_END and
# RAM_TOP falls back to RAM_END — the whole SRAM region is usable.  The linker
# scripts below enforce the real invariants: boot scratch/stack, the kernel
# image, and the CCP/TPA must all fit under __ram_top — a clashing IO_BASE or
# RAM_BASE therefore fails the link, never producing a broken image.
RAM_BASE_DEC=$((CONFIG_RAM_BASE))
RAM_END_DEC=$((CONFIG_RAM_BASE + CONFIG_RAM_SIZE))
IO_BASE_DEC=$((CONFIG_IO_BASE))
if [ "$RAM_END_DEC" -lt "$IO_BASE_DEC" ]; then
    RAM_TOP_DEC=$RAM_END_DEC
else
    RAM_TOP_DEC=$IO_BASE_DEC
fi
TPA_BASE_DEC=$((CONFIG_RAM_BASE + 0x100))
IO_BASE_HEX=$(printf '0x%X' "$IO_BASE_DEC")
RAM_TOP_HEX=$(printf '0x%X' "$RAM_TOP_DEC")
TPA_BASE_HEX=$(printf '0x%X' "$TPA_BASE_DEC")

CC=${CONFIG_CROSS_COMPILE}gcc
LD=${CONFIG_CROSS_COMPILE}ld
OBJCOPY=${CONFIG_CROSS_COMPILE}objcopy
OBJDUMP=${CONFIG_CROSS_COMPILE}objdump
AR=${CONFIG_CROSS_COMPILE}ar

ARCH_FLAGS="$CONFIG_ARCH_CFLAGS"
LIBGCC=$($CC $ARCH_FLAGS -print-libgcc-file-name)

CFLAGS="$ARCH_FLAGS -ffreestanding -nostdlib \
        -Os -ffunction-sections -fdata-sections \
        -fno-builtin -fomit-frame-pointer \
        -Wall -Wextra"
LDFLAGS="--gc-sections --strip-debug --no-warn-rwx-segments -m $CONFIG_LD_EMULATION"

PLATFORM_INC="-I platform/$PLATFORM_DIR"
BOOT_INC="$GEN_INC -I core/kernel/ -I core/ -I sdk/include $PLATFORM_INC"
KERNEL_INC="$GEN_INC -I core/kernel/ -I sdk/include -I core/ -I ./ $PLATFORM_INC"
CCP_INC="$GEN_INC -I core/ccp/ -I core/kernel/ -I sdk/include -I core/ -I ./ $PLATFORM_INC"
SDK_INC="$GEN_INC -I sdk/include -I core/kernel/ -I core/ -I ./ $PLATFORM_INC"

# Linker scripts cannot include C headers, so the kernel links receive the
# platform's CONFIG_STACK_SIZE as --defsym=__stack_size (below); the PROVIDE
# in linker_kernel_common.ld is only a hand-link safety net.

compile() {
    mkdir -p "$(dirname "$3")"
    $CC $1 -c "$2" -o "$3"
}

mkdir -p "$BUILD" "$INT" "$SDK_LIB"

# ── Bootloader ─────────────────────────────────────────────
echo "  Building bootloader..."
$CC $CFLAGS $BOOT_INC \
    -c "platform/$PLATFORM_DIR/bios.c" -o "$INT/boot_plat.o"

# When XIP_BASE must be auto-derived (--xip, no CONFIG_XIP_BASE), the boot
# jump target (XIP_TARGET) depends on __boot_size, which is only known after
# the first boot link.  Link once with a zero placeholder, extract the boot
# size, resolve the XIP geometry, then relink with the real target.  When the
# XIP base is explicit (or XIP is off), a single link suffices.
BOOT_RELINK=0
XIP_TARGET=0
if [ "$IS_XIP" = "1" ] && [ -n "$XIP_BASE" ]; then
    KERN_XIP_BASE_DEC=$((XIP_BASE + KERN_START_SEC * DISK_SECTOR_SIZE))
    XIP_TARGET=$(printf '0x%X' "$KERN_XIP_BASE_DEC")
fi
[ "$IS_XIP" = "1" ] && [ -z "$XIP_BASE" ] && BOOT_RELINK=1

$CC $CFLAGS $GEN_INC -I arch/$CONFIG_ARCH/ -I core/kernel/ -I core/ \
    -Wl,--gc-sections -Wl,--strip-debug -Wl,--no-warn-rwx-segments \
    -Wl,--defsym=__io_base="$IO_BASE_HEX" \
    -Wl,--defsym=__ram_top="$RAM_TOP_HEX" \
    -Wl,--defsym=__ram_base="$CONFIG_RAM_BASE" \
    -Wl,--defsym=__boot_base="$CONFIG_BOOT_BASE" \
    -Wl,--defsym=__xip_base="$XIP_TARGET" \
    -T arch/$CONFIG_ARCH/linker_boot.ld \
    arch/$CONFIG_ARCH/boot.S "$INT/boot_plat.o" -o "$INT/bootloader.elf"

# Read __boot_size from the linker (PROVIDE default or platform override).
BOOT_SIZE_HEX=$($OBJDUMP -t "$INT/bootloader.elf" | awk '/[[:space:]]__boot_size$/{print "0x"$1}')
BOOT_SIZE_DEC=$(printf '%d' "$BOOT_SIZE_HEX")

# Resolve the XIP base and placement geometry now that the boot size is
# known.  The kernel's in-place code starts at XIP_BASE + KERN_START_SEC*512
# (right past the boot and VMAP sectors on the disk); __kernel_xip_end is
# defined by the kernel link itself and flows to the CCP link.  Zero on
# non-XIP so a stray S0_XIP flag traps in the bootloader guard instead of
# jumping to junk.
if [ "$IS_XIP" = "1" ] && [ -z "$XIP_BASE" ]; then
    XIP_BASE=$((CONFIG_BOOT_BASE + BOOT_SIZE_DEC))
fi
XIP_BASE_HEX=$(printf '0x%X' "$XIP_BASE")
XIP_DEFSYM="--defsym=XIP_BASE=$XIP_BASE_HEX"
KERN_XIP_BASE=0x0000
KERN_XIP_SYM=
XIP_TARGET=0
if [ "$IS_XIP" = "1" ]; then
    KERN_XIP_BASE_DEC=$((XIP_BASE + KERN_START_SEC * DISK_SECTOR_SIZE))
    KERN_XIP_BASE=$(printf '0x%X' "$KERN_XIP_BASE_DEC")
    KERN_XIP_SYM="--defsym=__kernel_xip_base=$KERN_XIP_BASE"
    XIP_TARGET=$KERN_XIP_BASE
fi

# Relink the bootloader with the real XIP target when auto-derived.
if [ "$BOOT_RELINK" = "1" ]; then
    $CC $CFLAGS $GEN_INC -I arch/$CONFIG_ARCH/ -I core/kernel/ -I core/ \
        -Wl,--gc-sections -Wl,--strip-debug -Wl,--no-warn-rwx-segments \
        -Wl,--defsym=__io_base="$IO_BASE_HEX" \
        -Wl,--defsym=__ram_top="$RAM_TOP_HEX" \
        -Wl,--defsym=__ram_base="$CONFIG_RAM_BASE" \
        -Wl,--defsym=__boot_base="$CONFIG_BOOT_BASE" \
        -Wl,--defsym=__xip_base="$XIP_TARGET" \
        -T arch/$CONFIG_ARCH/linker_boot.ld \
        arch/$CONFIG_ARCH/boot.S "$INT/boot_plat.o" -o "$INT/bootloader.elf"
fi

$OBJCOPY -O binary --only-section=.boot "$INT/bootloader.elf" "$BUILD/bootloader.bin"
SIZE=$(wc -c < "$BUILD/bootloader.bin")
if [ "$SIZE" -gt "$BOOT_SIZE_DEC" ]; then
    echo "ERROR: bootloader.bin $SIZE bytes > __boot_size $BOOT_SIZE_DEC" >&2
    exit 1
fi

# ── Kernel (two-pass) ──────────────────────────────────────
echo "  Building kernel..."
KERNEL_C="core/kernel/main.c core/kernel/kernel.c core/kernel/bdos.c \
          core/kernel/disk.c platform/$PLATFORM_DIR/bios.c \
          sdk/src/ctype.c sdk/src/string.c sdk/src/stdio.c sdk/src/fs.c sdk/src/stdlib.c"
# arch/$CONFIG_ARCH/crt0.S is a required per-architecture C runtime entry.
# arch/$CONFIG_ARCH/kjump.S is OPTIONAL: an ISA with address-encoded
# execution-state (e.g. Cortex-M Thumb bit0) provides a strong override of
# the generic weak kjump() in kernel.c.  Its absence here is not an error.
KERNEL_S="arch/$CONFIG_ARCH/crt0.S"
[ -f "arch/$CONFIG_ARCH/kjump.S" ] && KERNEL_S="$KERNEL_S arch/$CONFIG_ARCH/kjump.S"

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
    --defsym=__stack_size="$CONFIG_STACK_SIZE" \
    $XIP_DEFSYM $KERN_XIP_SYM \
    -T $KERN_LD \
    $KERNEL_OBJS "$LIBGCC" -o "$INT/kernel_pass1.elf"

KERN_TOTAL_HEX=$($OBJDUMP -t "$INT/kernel_pass1.elf" | awk '/[[:space:]]__kernel_total$/{print "0x"$1}')
KERN_TOTAL=$(printf "%d" "$KERN_TOTAL_HEX")
KERN_START=$(((RAM_TOP_DEC - KERN_TOTAL) & ~3))
KERN_START_HEX=0x$(printf '%x' "$KERN_START")

$LD $LDFLAGS \
    --defsym=__KERN_START="$KERN_START_HEX" \
    --defsym=__io_base="$IO_BASE_HEX" \
    --defsym=__ram_top="$RAM_TOP_HEX" \
    --defsym=__tpa_base="$TPA_BASE_HEX" \
    --defsym=__stack_size="$CONFIG_STACK_SIZE" \
    $XIP_DEFSYM $KERN_XIP_SYM \
    -T $KERN_LD \
    $KERNEL_OBJS "$LIBGCC" -o "$INT/kernel.elf"

$OBJCOPY -O binary "$INT/kernel.elf" "$INT/kernel.bin"

# CCP XIP geometry.  The XIP SDK linker script sizes its XIP_REGION and RAM
# regions from these (kernel.elf symbols), but those symbols arrive via
# --just-symbols and GNU ld will NOT reliably fold them into MEMORY geometry
# (it reports "invalid origin for memory region XIP_REGION" and silently uses
# 0).  So we extract them here and pass them back as --defsym constants — the
# same mechanism the kernel scripts already use and that provably works.
# User .com files never link against the XIP script (they always run from the
# TPA), so only the CCP consumes this geometry.
SDK_GEOM=
if [ "$IS_XIP" = "1" ]; then
    XIP_END_HEX=$($OBJDUMP -t "$INT/kernel.elf" | awk '/[[:space:]]__kernel_xip_end$/{print "0x"$1}')
    if [ -z "$XIP_END_HEX" ]; then
        echo "ERROR: __kernel_xip_end not found in $INT/kernel.elf" >&2
        exit 1
    fi
    TPA_LEN_HEX=$(printf '0x%X' "$((KERN_START - TPA_BASE_DEC))")
    SDK_GEOM="--defsym=__kernel_xip_end=$XIP_END_HEX --defsym=__tpa_len=$TPA_LEN_HEX"
fi

# ── SDK libc ───────────────────────────────────────────────
echo "  Building SDK libc..."
SDK_LIBC_SRCS="sdk/src/ctype.c sdk/src/stdio.c sdk/src/string.c sdk/src/stdlib.c sdk/src/fs.c sdk/src/ccplib.c sdk/src/start.c"
SDK_LIBC_OBJS=
for src in $SDK_LIBC_SRCS; do
    obj="$SDK_OBJ/$(basename "$src" .c).o"
    compile "$CFLAGS $SDK_INC" "$src" "$obj"
    SDK_LIBC_OBJS="$SDK_LIBC_OBJS $obj"
done
compile "$CFLAGS $SDK_INC" arch/$CONFIG_ARCH/crt0.S "$SDK_OBJ/crt0.o"
$AR rcs "$SDK_LIB/libc.a" $SDK_LIBC_OBJS

# ── CCP ───────────────────────────────────────────────────
echo "  Building CCP..."
CCP_C="sdk/src/start.c sdk/src/ccplib.c \
       core/ccp/ccp.c core/ccp/cmd_files.c core/ccp/cmd_system.c \
       sdk/src/ctype.c sdk/src/string.c sdk/src/stdio.c sdk/src/fs.c sdk/src/stdlib.c"
CCP_OBJS=
for src in $CCP_C; do
    obj="$CCP_OBJ/${src%.c}.o"
    compile "$CFLAGS $CCP_INC" "$src" "$obj"
    CCP_OBJS="$CCP_OBJS $obj"
done
$LD $LDFLAGS -T $SDK_LD \
    $CCP_OBJS "$SDK_OBJ/crt0.o" "$LIBGCC" \
    --just-symbols="$INT/kernel.elf" $XIP_DEFSYM $SDK_GEOM -o "$INT/ccp.elf"
$OBJCOPY -O binary "$INT/ccp.elf" "$INT/ccp.bin"

printf '%s' "$PLATFORM_DIR"   > "$BUILD/.platform_dir"
printf '%s' "$CONFIG_ID"      > "$BUILD/.platform_id"
printf '%s' "$CONFIG_ARCH"    > "$BUILD/.arch"
printf '%s' "$CONFIG_ARCH_CFLAGS" > "$BUILD/.archflags"
printf '%s' "$IS_XIP"         > "$BUILD/.xip"
printf '%s' "$CONFIG_VOL_MAX"   > "$BUILD/.vol_max"
printf '%s' "$CONFIG_DISK_SIZE" > "$BUILD/.disk_size_kb"
printf '%s' "$CONFIG_FCB_MAX"   > "$BUILD/.fcb_max"
printf '%s' "$BOOT_SIZE_DEC"   > "$BUILD/.boot_size"
# App-selection tags: a knob that is unset means "all" (stamped as '*'),
# a declared value is stamped verbatim — "*" = all, "" = none, "a b" = filter.
if [ "${CONFIG_SYS_APPS+x}" = x ]; then
    SYS_APPS_TAG="$CONFIG_SYS_APPS"
else
    SYS_APPS_TAG="*"
fi
if [ "${CONFIG_EXTRA_APPS+x}" = x ]; then
    EXTRA_APPS_TAG="$CONFIG_EXTRA_APPS"
else
    EXTRA_APPS_TAG="*"
fi
printf '%s' "$SYS_APPS_TAG"   > "$BUILD/.sys_apps"
printf '%s' "$EXTRA_APPS_TAG" > "$BUILD/.extra_apps"