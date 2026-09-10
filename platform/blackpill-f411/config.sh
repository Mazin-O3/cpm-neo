# platform/blackpill-f411/config.sh
# CP/M Neo platform metadata — sourced by build_disk.sh / app_build.sh.
#
# WeAct Studio "Black Pill" STM32F411CEU6 (cortex-m / Cortex-M4F).
#
#   RAM    128 KB SRAM @ 0x20000000
#   FLASH  512 KB @ 0x08000000 (boot + XIP window + disk image share this)
#   CPU    96 MHz (PLL: 25 MHz HSE, M=25 N=192 P=2) — bios.c
#   Console USB CDC-ACM (OTG_FS, PA11/PA12, native USB-C port)
#   Storage Read-only: the XIP disk image (VMAP/kernel/CCP/apps) is
#          memcpy'd from flash; phase 1 has no write/erase support.
#   Time   DWT cycle counter
#   LED    PC13 (active-low heartbeat)
#
# Memory layout inside the 512 KB flash:
#   0x08000000  bootloader (4 KB boot budget from linker PROVIDE default;
#               pack.sh pads it to that size)
#   0x08001000  XIP window base (auto-derived as BOOT_BASE + boot size when
#               'sysgen new --xip' is used): the disk image — VMAP, kernel,
#               CCP, and all user files are stored here verbatim as the
#               concatenated sysgen disk image, so the kernel and CCP run in
#               place from flash.
#
# CONFIG_DISK_SIZE must be a multiple of 8 (kB) and, together with the
# boot budget, must fit the flash: 4 (boot) + CONFIG_DISK_SIZE <= 512.
# 504 is the largest multiple of 8 that leaves the 4 KB boot budget.

# Platform identity and architecture
CONFIG_ID="BPF411"
CONFIG_ARCH=cortex-m

# Memory configuration
CONFIG_RAM_SIZE=0x20000
CONFIG_RAM_BASE=0x20000000
CONFIG_IO_BASE=0x50000000
CONFIG_BOOT_BASE=0x08000000

# Disk and volume configuration
CONFIG_DISK_SIZE=504
CONFIG_VOL_MAX=4
CONFIG_FCB_MAX=4
CONFIG_STACK_SIZE=0x1000

# Application installation
CONFIG_SYS_APPS="dump help stat sys"
CONFIG_EXTRA_APPS="basic"