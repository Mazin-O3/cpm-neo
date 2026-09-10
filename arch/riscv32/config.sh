#!/usr/bin/env sh
# arch/riscv32/config.sh
# Architecture metadata — sourced by build_disk.sh / app_build.sh after
# the platform config declares CONFIG_ARCH=riscv32.  Supplies the cross
# toolchain prefix and the concrete compiler flags for this ISA.

# Required — the toolchain prefix for this ISA.  The build fails if unset.
CONFIG_CROSS_COMPILE=riscv64-unknown-elf-
CONFIG_ARCH_CFLAGS="-march=rv32im -mabi=ilp32"
CONFIG_LD_EMULATION="elf32lriscv"