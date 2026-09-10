# platform/vemu/config.sh
# CP/M Neo platform metadata — sourced by build_disk.sh / app_build.sh.
#
# Supplies the platform facts everything else is derived from.  All
# parameters carry the CONFIG_ prefix; most are required (there are no
# defaults).  The optional ones may be absent: CONFIG_XIP_BASE (omit for
# plain non-XIP builds, declare it to select XIP builds) and the two
# app-selection knobs (see the end of this file).
#
#   CONFIG_ID         — 8-char max OS platform id stamped into S0_PLATFORM
#                     (the platform identity used by --platform)

#   CONFIG_ARCH       — ISA directory under arch/ (selects the toolchain)

#   CONFIG_RAM_SIZE   — Total RAM in bytes (hex), e.g. 0x10000 = 64 KB

#   CONFIG_RAM_BASE   — Base address of the RAM region holding CP/M Neo
#                       (TPA + kernel), independent of how the CPU addresses it

#   CONFIG_IO_BASE    — Base address of the peripheral MMIO window

#   CONFIG_BOOT_BASE  — Address where the bootloader is placed and executed
#                       (the reset vector origin)

#   CONFIG_BOOT_SIZE  — Optional. Boot code budget override; arch linker script
#                       provides default via PROVIDE()

#   CONFIG_BOOT_RAM_SIZE — Optional. Boot runtime RAM budget override; arch
#                       linker script provides default via PROVIDE()

#   CONFIG_XIP_BASE   — Optional.  XIP window base for 'sysgen new --xip'.
#                       The kernel/CCP run in place from this window once
#                       --xip enables XIP; when omitted and --xip is given,
#                       the base is auto-derived as BOOT_BASE + boot size.
#                       Declare it only when the window is NOT right past
#                       boot.  The XIP window has no configured size: it
#                       extends from the XIP base to the end of the on-disk
#                       kernel/CCP contents, and the flash simply maps the
#                       XIP disk image

#   CONFIG_VOL_MAX    — Volume count, A:..P; required.  Must not exceed
#                       The sysgen host ceiling of 16

#   CONFIG_DISK_SIZE  — Total disk image size in KB, overhead included: the
#                       boot/VMAP and kernel+CCP sectors come out of this
#                       budget first, and the remaining block grid is divided
#                       between the CONFIG_VOL_MAX volumes.  Must be a
#                       multiple of 8 and must not exceed the host ceiling of
#                       32768 (32 MB)

#   CONFIG_FCB_MAX    — Open-file control blocks (kernel RAM); must not
#                       exceed the host ceiling of 8

#   CONFIG_STACK_SIZE — Shared stack bytes (kernel/CCP/apps; linker-only)

#   CONFIG_SYS_APPS   — Optional.  Space-separated list of apps/sys commands
#                       for 'sysgen new' to install on the disk, e.g.
#                       'copy stat'.  Unset or '*' = install ALL of them;
#                       empty "" = install NONE.

#   CONFIG_EXTRA_APPS — Optional.  Space-separated list of apps/extra apps,
#                       e.g. 'basic ed'.  Unset or '*' = install ALL of them;
#                       empty "" = install NONE.

# Platform identity and architecture
CONFIG_ID="vemu"
CONFIG_ARCH=riscv32

# Memory configuration
CONFIG_RAM_SIZE=0x10000
CONFIG_RAM_BASE=0x0000
CONFIG_IO_BASE=0xFF00
CONFIG_BOOT_BASE=0x0000
CONFIG_XIP_BASE=0x10000

# Disk and volume configuration
CONFIG_DISK_SIZE=2048
CONFIG_VOL_MAX=4
CONFIG_FCB_MAX=4
CONFIG_STACK_SIZE=0x1000

# Application installation
CONFIG_SYS_APPS="*"
CONFIG_EXTRA_APPS="basic ed"