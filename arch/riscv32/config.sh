#!/usr/bin/env sh
# arch/riscv32/config.sh
# Architecture metadata — sourced by build_disk.sh / app_build.sh after
# the platform config declares ARCH=riscv32.  Supplies the cross toolchain
# prefix and the concrete compiler flags for this ISA.

# Required — the toolchain prefix for this ISA.  The build fails if unset.
CROSS_COMPILE=riscv64-unknown-elf-
ARCH_CFLAGS="-march=rv32im -mabi=ilp32"
LD_EMULATION="elf32lriscv"

# Bootloader placement and footprint — arch constants for this ISA.
BOOT_BASE=0x0000        # Address where boot code is placed/executed (reset vector)
BOOT_SIZE=1024          # Maximum boot code image bytes
BOOT_RAM_SIZE=0x400     # Boot runtime RAM: scratch + stack + bios .bss
