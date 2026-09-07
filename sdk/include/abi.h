/*
 * sdk/include/abi.h — CP/M Neo system interface
 *
 * The user-facing ABI shared by the kernel, the SDK, the CCP, and user
 * programs: filename conventions, console keys, volume/user context,
 * file descriptors, argument passing, file/volume statistics, and
 * environment slots.
 *
 * Tunable parameters are sourced from core/config.h.  On-disk layout
 * constants (sector size, sector 0, VMAP / volume header) are owned by
 * core/kernel/disk_format.h; user programs reach it only transitively
 * through this header and reference just the disk properties they
 * observe (DISK_SECTOR_SIZE).
 */

#ifndef ABI_H
#define ABI_H

#include <stdint.h>
#include "config.h"
#include "disk_format.h"

/* Filename constants */
#define NAME83_BASE  8
#define NAME83_EXT   3
#define NAME83_LEN  (NAME83_BASE + NAME83_EXT)
#define FILENAME_MAX 13 /* 8.3 name + NUL terminator */

/* Console control-key conventions shared by the kernel, SDK and apps. */
#define CH_BREAK 0x03 /* ^C — Break a running program                  */
#define CH_EOF   0x1A /* ^Z — End-of-file marker in text files         */
#define CH_ESC   0x1B /* ESC — Abort listings / quit pager             */

/* Volume names */
#define VOL_A 0
#define VOL_B 1
#define VOL_C 2
#define VOL_D 3
#define MAX_VOLUMES CONFIG_VOL_MAX
#define VOL_INVALID -1

/* Volume/user context */
typedef struct
{
    int8_t vol_id;      /* Current volume (0..MAX_VOLUMES-1, or VOL_INVALID) */
    uint8_t user_area;  /* Current user area (0..USER_AREA_MAX)       */
} FsContext;

/* File-descriptor constants */

#define FD_STDIN 0
#define FD_STDOUT 1
#define FD_STDERR 2
#define FD_FILE_BASE 3

static inline int fd_is_console(int fd)
{
    return fd < FD_FILE_BASE;
}
static inline int fd_is_stdin(int fd)
{
    return fd == FD_STDIN;
}

#define USER_AREA_MAX 15

#define ARGS_MAX 8
#define ARG_LEN_MAX 32

typedef struct
{
    int argc;                           /* Argument count (0-8) */
    char argv[ARGS_MAX][ARG_LEN_MAX];  /* Null-terminated arg strings */
} ArgBlock;

typedef struct
{
    uint32_t size;              /* File size in bytes */
    char name[FILENAME_MAX];   /* Null-terminated 8.3 name */
    uint8_t attrib;             /* FILE_ATTR_READ_ONLY | FILE_ATTR_SYSTEM */
    uint8_t user_area;          /* User area that owns the file */
    uint16_t extents;           /* Number of 8 KB extents */
    uint32_t alloc_bytes;       /* Allocated space in bytes */
} FileInfo;

#define FILE_ATTR_READ_ONLY 0x01
#define FILE_ATTR_SYSTEM    0x02

#define VOL_ATTR_RW 0
#define VOL_ATTR_RO 1

typedef struct
{
    uint16_t total_blocks;        /* Usable 1 KB data blocks      */
    uint16_t free_blocks;         /* Free 1 KB data blocks        */
    uint8_t read_only;            /* VOL_ATTR_RO or VOL_ATTR_RW  */
} VolStat;

typedef struct
{
    uint32_t tpa;                     /* Transient program area base */
    uint16_t os_version;              /* CP/M Neo version */
    uint16_t kern_version;            /* Kernel build version */
    uint16_t ccp_version;             /* CCP build version */
    uint16_t disk_size_kb;            /* Block grid capacity (KB) */
    uint16_t disk_unalloc_kb;         /* Unallocated pool (KB) */
    uint8_t  vol_mounted[MAX_VOLUMES];    /* 1 = mounted                    */
    char     platform[9];             /* Platform name, NUL-terminated  */
    uint8_t  xip;                     /* 1 = XIP disk image             */
} SysInfo;

/* Sector size is a disk *property* user programs observe (dsk size math);
 * the value itself is owned by core/kernel/disk_format.h (via the include
 * at the top of this header). */

/* Little-endian byte accessors.  Shared by the kernel and the sysgen host
 * tool (parsing on-disk structures and ELF headers); present here so every
 * consumer gets them from one place.  Unreferenced in a TU they compile to
 * nothing. */
static inline uint16_t read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void write16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Environment slot indices */

#define ENV_RETURN_CODE  0   /* Return code of last program/command */
#define ENV_BATCH_OFFSET 1   /* Offset of batch file in CCP */
#define ENV_USER_DEFINED 2   /* User-defined environment slot */
#define ENV_SLOTS_MAX    3

#endif /* ABI_H */