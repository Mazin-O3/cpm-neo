/*
 * sdk/include/fsctx.h — CP/M Neo filesystem context and process ABI
 *
 * Volume/user context, filename conventions, file types, file descriptors,
 * argument passing.  The single source of truth for the types and constants
 * shared by the kernel, the SDK, the CCP, and user programs.
 */

#ifndef SDK_FSCTX_H
#define SDK_FSCTX_H

#include <stdint.h>

#include "disk_format.h"

/*
 * DISK_SECTOR_SIZE (via disk_format.h above) is a disk *property* user
 * programs observe (disk-size math); the canonical value lives in
 * core/kernel/disk_format.h so boot, kernel, sysgen, and user programs

 * all agree on one number.
 */
/*
 * File naming
 */
#define NAME83_BASE  8
#define NAME83_EXT   3
#define NAME83_LEN   (NAME83_BASE + NAME83_EXT)
#define FILENAME_MAX 13 /* 8.3 name + NUL terminator */

/*
 * Volume / user context
 */
#define VOL_A       0
#define VOL_B       1
#define VOL_C       2
#define VOL_D       3
#define MAX_VOLUMES CONFIG_VOL_MAX
#define VOL_INVALID -1

#define USER_AREA_MAX 15

typedef struct
{
    int8_t  vol_id;    /* Current volume (0..MAX_VOLUMES-1, or VOL_INVALID) */
    uint8_t user_area; /* Current user area (0..USER_AREA_MAX)              */
} FsContext;

/*
 * Volume statistics
 */
#define VOL_ATTR_RW 0
#define VOL_ATTR_RO 1

typedef struct
{
    uint16_t total_blocks; /* Usable 1 KB data blocks      */
    uint16_t free_blocks;  /* Free 1 KB data blocks        */
    int      read_only;    /* VOL_ATTR_RO or VOL_ATTR_RW  */
} VolStat;

/*
 * File statistics
 */
#define FILE_ATTR_READ_ONLY 0x01
#define FILE_ATTR_SYSTEM    0x02

typedef struct
{
    uint32_t size;               /* File size in bytes */
    char     name[FILENAME_MAX]; /* Null-terminated 8.3 name */
    uint8_t  attrib;             /* FILE_ATTR_READ_ONLY | FILE_ATTR_SYSTEM */
    uint8_t  user_area;          /* User area that owns the file */
    uint16_t extents;            /* Number of 8 KB extents */
    uint32_t alloc_bytes;        /* Allocated space in bytes */
} FileInfo;

/*
 * File descriptors
 */
#define FD_STDIN     0
#define FD_STDOUT    1
#define FD_STDERR    2
#define FD_FILE_BASE 3

static inline int fd_is_console(int fd)
{
    return fd < FD_FILE_BASE;
}

static inline int fd_is_stdin(int fd)
{
    return fd == FD_STDIN;
}

/*
 * Argument passing
 */
#define ARGS_MAX    8
#define ARG_LEN_MAX 32

typedef struct
{
    int  argc;                        /* Argument count (0-8) */
    char argv[ARGS_MAX][ARG_LEN_MAX]; /* Null-terminated arg strings */
} ArgBlock;

#endif /* SDK_FSCTX_H */
