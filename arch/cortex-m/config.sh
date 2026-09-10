#!/usr/bin/env sh
# arch/cortex-m/config.sh
# Architecture metadata — sourced by build_disk.sh / app_build.sh after
# the platform config declares CONFIG_ARCH=cortex-m.  Supplies the cross
# toolchain prefix and the concrete compiler flags for this ISA.

# Required — the toolchain prefix for this ISA.  The build fails if unset.
CONFIG_CROSS_COMPILE=arm-none-eabi-
CONFIG_ARCH_CFLAGS="-mthumb -mcpu=cortex-m4 -mfloat-abi=soft -ffreestanding"
CONFIG_LD_EMULATION="armelf"