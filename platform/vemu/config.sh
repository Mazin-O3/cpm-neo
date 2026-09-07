# platform/vemu/config.sh
# CP/M Neo platform metadata — sourced by build_disk.sh / app_build.sh.
#
# Supplies the platform facts everything else is derived from.  All
# parameters carry the CONFIG_ prefix; all are required (there are no
# defaults).  Only CONFIG_XIP_BASE may be absent, and only when --xip
# builds are not used.
#
#   CONFIG_ID         — 8-char max OS platform id stamped into S0_PLATFORM
#                     (the platform identity used by --platform)

#   CONFIG_ARCH       — ISA directory under arch/ (selects the toolchain)

#   CONFIG_RAM_SIZE   — Total RAM in bytes (hex), e.g. 0x10000 = 64 KB

#   CONFIG_RAM_BASE   — Base address of the RAM region holding CP/M Neo
#                       (TPA + kernel), independent of how the CPU addresses it

#   CONFIG_IO_BASE    — Base address of the peripheral MMIO window

#   CONFIG_XIP_BASE   — Flash/XIP window base; used only by `sysgen new --xip`
#                       builds (the kernel/CCP run in place from this window)
#                       Plain builds (no --xip) ignore it.
#                       The XIP window has no configured size: it extends from
#                       the XIP base to the end of the on-disk kernel/CCP
#                       contents, and the flash simply maps the XIP disk image

#   CONFIG_VOL_MAX    — Volume count, A:..P; required.  Must not exceed
#                       The sysgen host ceiling of 16

#   CONFIG_DISK_SIZE  — Per-volume disk-size cap in KB; the block map /
#                       volume cap derives from it (CONFIG_DISK_SIZE/8 bitmap
#                       bytes, CONFIG_DISK_SIZE blocks).  Must be a multiple
#                       of 8 and must not exceed the host ceiling of 32768 (32 MB)

#   CONFIG_FCB_MAX    — Open-file control blocks (kernel RAM); must not
#                       exceed the host ceiling of 8

#   CONFIG_STACK_SIZE — Shared stack bytes (kernel/CCP/apps; linker-only)

CONFIG_ID="vemu"
CONFIG_ARCH=riscv32
CONFIG_RAM_SIZE=0x10000
CONFIG_RAM_BASE=0x0000
CONFIG_IO_BASE=0xFF00
CONFIG_XIP_BASE=0x10000

# Each platform must declare all four:
CONFIG_VOL_MAX=4
CONFIG_DISK_SIZE=2048
CONFIG_FCB_MAX=4
CONFIG_STACK_SIZE=0x1000