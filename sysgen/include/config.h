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
 *     the per-volume alloc bitmap).  These defines size those arrays; the
 *     values are the largest any current platform declares, and they are
 *     the enforced maximum (cmd_new rejects a platform whose values exceed
 *     them).
 *   - A platform's ACTUAL values live only in platform/<ID>/config.sh;
 *     build_disk.sh / app_build.sh hand them to target compiles as -D
 *     defines.  This header exists only for the host build, force-included
 *     via `-include config.h` (Makefile).  There is no generated config.h.
 *
 * Host ceilings (the maximum a platform may declare):
 *   CONFIG_VOL_MAX       4     volumes (A:..D)
 *   CONFIG_DISK_SIZE 32768     KB total image / block grid (ceil(KB/8)
 *                              bitmap bytes); u16 on-disk fields cap the
 *                              grid at 65535
 *   CONFIG_FCB_MAX       4     open-file control blocks (kernel RAM only;
 *                              no on-disk constraint)
 */

#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_VOL_MAX   4     /* Host ceiling: platform CONFIG_VOL_MAX   */
#define CONFIG_DISK_SIZE 32768 /* Host ceiling in KB/vol: bitmap = /8 B   */
#define CONFIG_FCB_MAX   4     /* Host ceiling: platform CONFIG_FCB_MAX   */

#endif /* CONFIG_H */