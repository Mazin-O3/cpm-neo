#ifndef SYSGEN_H
#define SYSGEN_H

#include <stddef.h>
#include <stdint.h>

#include "abi.h"
#include "disk_format.h"

/* Image buffer owned by bios_host.c */
void     sysgen_set_disk(uint8_t *disk, uint32_t size);
uint8_t *sysgen_disk(void);
uint32_t sysgen_disk_size(void);

/* ELF32 symbol lookup (for __kernel_base) */
int elf32_symbol(const uint8_t *elf, size_t len, const char *name, uint32_t *value);

/* Convert a host name to an 11-char padded 8.3 string */
void to_name83(const char *src, char *out83);

/* Active disk configuration for the platform being generated.  The sysgen
 * host is compiled once at its ceilings (sysgen/include/config.h); the
 * platform's effective values arrive at runtime from the build tags written
 * by build_disk.sh (.vol_max, .disk_size_kb). */
typedef struct
{
    uint16_t vol_count;    /* Active volume count (A:..)               */
    uint16_t disk_size_kb; /* CONFIG_DISK_SIZE: total image size in KB, */
                           /* overhead included.  It is also the whole- */
                           /* disk block-coverage bound for any one     */
                           /* volume (its alloc bitmap is /8 bytes)     */
} SysgenDiskCfg;

/* Defaults to the host compile-time ceilings; used when build tags are
 * missing. */
SysgenDiskCfg sysgen_disk_cfg_default(void);

/* Build a new disk image in place (fills sysgen_disk()); returns reserved
 * secs or -1.  size_kb is the TOTAL image size in KB (cfg->disk_size_kb):
 * the boot/VMAP and reserved kernel+CCP sectors come out of that budget
 * first, and the remaining block grid is divided equally between all
 * cfg->vol_count volumes, each one formatted (header + empty root) so every
 * volume is mounted at boot.  No volume's share can exceed the grid, which
 * is itself smaller than cfg->disk_size_kb, so the coverage clamp is a
 * backstop only. */
int mkdisk_build(const SysgenDiskCfg *cfg, uint32_t size_kb, const uint8_t *kern,
                 uint32_t kern_size, const uint8_t *ccp, uint32_t ccp_size, uint32_t kern_load,
                 uint16_t os_ver, uint16_t kern_ver, uint16_t ccp_ver, const char *platform,
                 int xip);

/* Minimum disk size (KB) so every volume can hold min-viable blocks */
int mkdisk_min_size_kb(const SysgenDiskCfg *cfg, uint32_t kern_size, uint32_t ccp_size, int xip);

/* Whole-file helpers */
int read_file(const char *path, uint8_t **out, uint32_t *out_len);
int write_file(const char *path, const uint8_t *data, uint32_t len);

#endif /* SYSGEN_H */