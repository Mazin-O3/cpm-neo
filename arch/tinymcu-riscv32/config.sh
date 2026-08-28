#!/usr/bin/env sh
# arch/tinymcu-riscv32/config.sh
# Architecture metadata, sourced by build_disk.sh / app_build.sh after
# --arch is resolved. Supplies the cross toolchain prefix and the concrete
# compiler flags for this ISA.
#

CROSS_COMPILE=${CROSS_COMPILE:-riscv64-unknown-elf-}
ARCH_CFLAGS="-march=rv32im -mabi=ilp32"
LD_EMULATION="elf32lriscv"
