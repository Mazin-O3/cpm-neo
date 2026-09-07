/*
 * core/config.h — CP/M Neo system configuration
 *
 * Single tunable surface for CP/M Neo's resource footprint.  A port tunes
 * the whole system by editing the values here; defaults match the stock
 * configuration.
 *
 * These values are consumed by the kernel, the disk layer, the SDK ABI
 * (sdk/include/abi.h), the CCP, and sysgen, so editing them changes the
 * whole system consistently — But note that the on-disk format is shared
 * between sysgen (image builder) and the runtime kernel: changing a disk
 * format parameter (CONFIG_VOL_MAX, CONFIG_BLOCK_MAP_BYTES) requires
 * regenerating the disk image with `sysgen new --platform=<name>`.
 *
 * Fixed, non-footprint constants (root directory entries, block runs per
 * volume, user-area count) are not tunables; they live in the headers
 * that own them (bdos.h, disk_format.h, abi.h).
 *
 * The shared kernel/user stack size (CONFIG_STACK_SIZE) is consumed by the
 * linker via --defsym from build_disk.sh (linker scripts cannot include C
 * headers); the PROVIDE fallback in linker_kernel_common.ld mirrors the
 * default here.  This header is assembly-safe (pure defines), so it can
 * be included from `.S` files via core/kernel/disk_format.h.
 */

#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_VOL_MAX           4       /* Volumes (A:..D:) — In SysInfo, disk layer, sysgen  */
#define CONFIG_BLOCK_MAP_BYTES   256     /* Alloc bitmap bytes per volume (RAM + vol capacity) */
#define CONFIG_FCB_MAX           4       /* Open-file control blocks (kernel RAM)              */
#define CONFIG_STACK_SIZE        0x1000  /* Shared stack bytes (kernel/CCP/apps)               */

#endif /* CONFIG_H */