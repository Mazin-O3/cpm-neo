# platform/vemu/config.sh
# CP/M Neo platform metadata — sourced by build_disk.sh / app_build.sh.
#
# Supplies the platform facts everything else is derived from:
#   ID        — 8-char max OS platform id stamped into S0_PLATFORM
#               (the platform identity used by --platform)
#   ARCH      — ISA directory under arch/ (selects the toolchain)
#   RAM_SIZE  — total RAM in bytes (hex), e.g. 0x10000 = 64 KB
#   RAM_BASE  — base address of the RAM region holding CP/M Neo
#               (TPA + kernel), independent of how the CPU addresses it
#   IO_BASE   — base address of the peripheral MMIO window
#   XIP_BASE  — flash/XIP window base; used only by `sysgen new --xip`
#               builds (the kernel/CCP run in place from this window).
#               Plain builds (no --xip) ignore it and are always non-XIP.
#               The XIP window has no configured size: it extends from
#               XIP_BASE to the end of the on-disk kernel/CCP contents, and
#               the flash simply maps the XIP disk image.

ID="vemu"
ARCH=riscv32
RAM_SIZE=0x10000
RAM_BASE=0x0000
IO_BASE=0xFF00
XIP_BASE=0x10000