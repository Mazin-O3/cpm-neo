/*
 * sysgen/include/config.h — Sysgen host capacity ceilings
 *
 * The sysgen tool is compiled ONCE and serves every platform, so these
 * compile-time values are the CEILINGS — the maximum the host may ever
 * handle — NOT defaults and NOT any platform's configuration.
 *
 *   - The kernel's real filesystem layer (bdos.c/disk.c) is compiled into
 *     the host, and its fixed-size C arrays must be dimensioned at sysgen's
 *     compile time (MAX_VOLUMES -> volume/FCB arrays, BD_BLOCK_MAP_BYTES ->
 *     the per-volume alloc bitmap).  These defines size those arrays to the
 *     largest any platform may declare; anything below a platform's needs
 *     is rejected at runtime and anything larger only costs host RAM.
 *   - A platform's ACTUAL values live only in platform/<ID>/config.sh.
 *     build_disk.sh requires every knob (no defaults) and stamps the
 *     effective values to build/gen/config.h (kernel/CCP/SDK builds) and to
 *     build tags (.vol_max, .disk_size_kb).  cmd_new reads those tags at
 *     runtime and applies them via SysgenDiskCfg + bd_set_block_cap()/
 *     disk_set_block_cap(); it REJECTS a platform whose values exceed these
 *     ceilings.
 *
 * Host ceilings (the maximum a platform may declare):
 *   CONFIG_VOL_MAX     16     volumes (A:..P)  — the VMAP sector holds 28
 *                             volume records, so 16 is well inside the wire
 *                             format limit
 * 
 *   CONFIG_DISK_SIZE 32768    KB per volume cap / total grid (32 MB); the
 *                             alloc bitmap derives as /8 bytes and must be
 *                             a multiple of 8 seeds the build's guard;
 *                             u16 on-disk fields (S0_DISK_SIZE_KB, block
 *                             grid) cap this at 65535
 * 
 *   CONFIG_FCB_MAX      8     open-file control blocks (kernel RAM only;
 *                             no on-disk constraint)
 *
 * There is no core/config.h: kernel/CCP/SDK/app builds use the generated
 * build/gen/config.h; the sysgen host uses this header
 */

#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_VOL_MAX      4     /* Host ceiling: platform CONFIG_VOL_MAX   */
#define CONFIG_DISK_SIZE    32768  /* Host ceiling in KB/vol: bitmap = /8 B   */
#define CONFIG_FCB_MAX      4      /* Host ceiling: platform CONFIG_FCB_MAX   */

#endif /* CONFIG_H */