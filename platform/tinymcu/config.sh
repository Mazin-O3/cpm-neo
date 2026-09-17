# platform/tinymcu/config.sh
# CP/M Neo platform metadata for TinyMCU — sourced by build_disk.sh /
# app_build.sh. See platform/vemu/config.sh for the full description of
# every CONFIG_ variable; only the TinyMCU-specific choices are called
# out below.

# Platform identity and architecture. TinyMCU is a plain RV32IM core
# (rtl/core/tinymcu_cpu.vhd), so it uses the shared arch/riscv32/
# unmodified — there is no platform-specific arch/ directory anymore
# (the old arch/tinymcu-riscv32/ only worked around problems this
# config.sh now solves generically: RAM not starting at 0x0, and a
# different -march, both handled below).
CONFIG_ID="tinymcu"
CONFIG_ARCH=riscv32

# Memory configuration, matching rtl/tinymcu_pkg.vhd exactly (the
# authoritative source):
#   ROM_BASE=0x00000000 (Boot ROM, IMEM_ADDR_WIDTH=13 -> 32 KB)
#   RAM_BASE=0x02000000 (kernel/TPA RAM, RAM_ADDR_WIDTH=14 -> 64 KB)
#   PERIPHERALS_BASE=0x04000000
CONFIG_BOOT_BASE=0x00000000
CONFIG_RAM_BASE=0x02000000
CONFIG_RAM_SIZE=0x10000
CONFIG_IO_BASE=0x04000000

# XIP window, only used when 'sysgen new' is given --xip. Declared
# explicitly (rather than left to auto-derive as BOOT_BASE + boot size)
# because it names a fixed hardware address, not a build-time layout
# choice: tinymcu_imem_xip.vhd's fetch engine always starts serving
# instructions at XIP_FLASH_BASE (rtl/tinymcu_pkg.vhd), regardless of
# how large the bootloader ends up being. Getting an XIP disk image's
# kernel/CCP sectors actually programmed into the real flash chip (or
# the RTL testbench's simulated one) ahead of boot is a separate piece
# of work, not yet wired up here.
CONFIG_XIP_BASE=0x00008000

# Disk and volume configuration. The only storage TinyMCU has right now
# is the RAM disk (see platform/tinymcu/bios.c): its own 128 KB SRAM
# block (RAMDISK_ADDR_WIDTH=15 in rtl/core/tinymcu_cpu.vhd), which
# starts empty on every reset since it isn't backed by anything
# persistent.
CONFIG_DISK_SIZE=128
CONFIG_VOL_MAX=1
CONFIG_FCB_MAX=4
CONFIG_STACK_SIZE=0x1000

# Application installation. TinyMCU's 128 KB disk has no room to spare
# for the bundled apps/sys and apps/extra sets on top of the kernel/CCP
# themselves.
CONFIG_SYS_APPS=""
CONFIG_EXTRA_APPS=""
