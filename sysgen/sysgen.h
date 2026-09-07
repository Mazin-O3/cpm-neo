#ifndef SYSGEN_H
#define SYSGEN_H

#include <stdint.h>
#include <stddef.h>

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

/* Build a new disk image in place (fills sysgen_disk()); returns reserved secs or -1.
 * Divides the block grid equally between all MAX_VOLUMES volumes (A:-D:) and
 * formats each one (header + empty root), so every volume is mounted at boot. */
int mkdisk_build(uint32_t size_kb,
                const uint8_t *kern, uint32_t kern_size,
                const uint8_t *ccp, uint32_t ccp_size,
                uint32_t kern_load,
                uint16_t os_ver, uint16_t kern_ver, uint16_t ccp_ver,
                const char *platform, int xip);

/* Minimum disk size (KB) so every volume can hold min-viable blocks */
int mkdisk_min_size_kb(uint32_t kern_size, uint32_t ccp_size, int xip);

/* Maximum useful disk size (KB): bounded by MAX_VOLUMES x the 2 MB volume cap,
 * plus the reserved area. */
int mkdisk_max_size_kb(uint32_t kern_size, uint32_t ccp_size, int xip);

/* Whole-file helpers */
int read_file(const char *path, uint8_t **out, uint32_t *out_len);
int write_file(const char *path, const uint8_t *data, uint32_t len);

#endif /* SYSGEN_H */