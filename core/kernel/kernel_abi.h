/*
 * kernel/kernel_abi.h — kernel userspace ABI
 *
 * Shared types, constants, and on-disk format definitions between the
 * kernel and transient commands.  Apps include this transitively via
 * syscall.h; the VMAP and volume header layouts are also consumed by
 * sysgen.
 */

#ifndef KERNEL_ABI_H
#define KERNEL_ABI_H

#include <stdint.h>
#include "s0_layout.h"

/* Filename constants */
#define NAME83_BASE 8
#define NAME83_EXT 3
#define NAME83_LEN (NAME83_BASE + NAME83_EXT)
#define FILENAME_MAX 13

/* Console control-key conventions shared by the kernel, SDK and apps. */
#define CH_BREAK 0x03 /* ^C — break a running program                  */
#define CH_EOF   0x1A /* ^Z — end-of-file marker in text files         */
#define CH_ESC   0x1B /* ESC — abort listings / quit pager             */

/* Volume names */
#define VOL_A 0
#define VOL_B 1
#define VOL_C 2
#define VOL_D 3
#define VOL_MAX 4
#define VOL_INVALID -1

/* Volume/user context */
typedef struct
{
    int8_t vol_id;      /* current volume (0-3, or VOL_INVALID) */
    uint8_t user_area;  /* current user area (0-15) */
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
    int argc;                           /* argument count (0-8) */
    char argv[ARGS_MAX][ARG_LEN_MAX];  /* null-terminated arg strings */
} ArgBlock;

typedef struct
{
    uint32_t size;              /* file size in bytes */
    char name[FILENAME_MAX];   /* null-terminated 8.3 name */
    uint8_t attrib;             /* FILE_ATTR_READ_ONLY | FILE_ATTR_SYSTEM */
    uint8_t user_area;          /* user area that owns the file */
    uint16_t extents;           /* number of 8 KB extents */
    uint32_t alloc_bytes;       /* allocated space in bytes */
} FileInfo;

#define FILE_ATTR_READ_ONLY 0x01
#define FILE_ATTR_SYSTEM    0x02

#define VOL_ATTR_RW 0
#define VOL_ATTR_RO 1

#define KERN_START_LBA 2 /* kernel image LBA */

typedef struct
{
    uint16_t total_blocks;        /* usable 1 KB data blocks      */
    uint16_t free_blocks;         /* free 1 KB data blocks        */
    uint8_t read_only;            /* VOL_ATTR_RO or VOL_ATTR_RW  */
} VolStat;

typedef struct
{
    uint32_t tpa;                     /* transient program area base */
    uint16_t os_version;              /* CP/M Neo version */
    uint16_t kern_version;            /* kernel build version */
    uint16_t ccp_version;             /* CCP build version */
    uint16_t disk_size_kb;            /* block grid capacity (KB) */
    uint16_t disk_unalloc_kb;         /* unallocated pool (KB) */
    uint8_t  vol_mounted[VOL_MAX];    /* 1 = mounted                    */
    char     platform[9];             /* platform name, NUL-terminated  */
} SysInfo;

/* Little-endian byte accessors */
static inline uint16_t read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void write16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/*
 * Syscall jump table — the kernel/SDK ABI.
 *
 * Field ORDER is the ABI: slot n lives at byte offset n * 4.  Hand-written
 * asm reaches entries through the table pointer published in environment
 * slot ENV_SYSCALL_PTR (see docs/syscall-reference.md for slot numbers).
 * Pre-alignment the table was renumbered freely (pre-1.0 ABI break);
 * treat the layout below as frozen — only append new fields at the end,
 * never reorder or remove.
 */
typedef struct
{
    int      (*open)(const char *name, uint8_t writable);
    int      (*read)(int fd, void *buf, uint32_t len);
    int      (*write)(int fd, const void *buf, uint32_t len);
    int      (*close)(int fd);
    void     (*exit)(int rc);
    int      (*args)(ArgBlock *out);
    int      (*findfile)(const char *pattern, FileInfo *out, uint16_t start_pos);
    uint32_t (*getsize)(int fd);
    int      (*create)(const char *name);
    int      (*delete)(const char *name);
    int      (*rename)(const char *old, const char *new);
    int      (*mount)(int8_t vol_id);
    int      (*unmount)(int8_t vol_id);
    int      (*resize)(int8_t vol_id, int16_t delta);
    int      (*vstat)(int8_t vol_id, VolStat *stat);
    int      (*exec)(const char *name, int argc, char **argv);
    int      (*dev)(uint32_t reg, uint32_t cmd, uint32_t *data);
    int      (*fsetattr)(const char *name, uint8_t attrib);
    int      (*info)(SysInfo *out);
    int      (*seek)(int fd, uint32_t offset);
    int      (*getctx)(FsContext *out);
    int      (*setctx)(FsContext ctx);
    uint32_t (*getenv)(uint8_t slot);
    int      (*setenv)(uint8_t slot, uint32_t value);
    int      (*vsetattr)(int8_t vol_id, uint8_t attr);
    uint32_t (*time)(void);
    int      (*sync)(void);
    int      (*consize)(uint8_t *cw, uint8_t *ch);
} SyscallTable;

/* IOCTL command encoding: [7] write=1/read=0, [5:0] offset (adds to reg) */
#define IOCTL_WRITE_FLAG 0x80 /* flag indicating a write operation */
#define IOCTL_OFF_MASK   0x3F /* mask to extract the offset from a command */

#define IO_SIZE 0x100

#define DISK_SECTOR_SIZE  512

#define DISK_MAGIC      0x4350u /* 'CP' — sector-0 identity     */
#define BOOT_SIG        0xAA55u   /* standard boot sector sig     */

#define BOOT_MAGIC DISK_MAGIC

/* Volume-map (VMAP) format — sector LBA 1.
 *
 * 0x000 u16 num_blocks  — total 1K blocks on disk
 * 0x002 u16 block_base   — LBA of block 0
 * 0x004 u16 magic       — VMAP_MAGIC
 * 0x006 VolRec[VOL_MAX] — 18 B each
 * 0x1FE u16             — BOOT_SIG
 *
 * Block i occupies LBAs [block_base + i*2, +2) (1 KB = 2 sectors).
 * A volume's logical space is the concatenation of its ordered block
 * runs. run_count == 0 means the volume is unmounted. */

#define VMAP_LBA         1 /* volume-map sector LBA            */
#define VMAP_MAGIC       0x4350u /* 'CP' identity              */
#define VOL_MAX_RUNS     4 /* max runs per volume           */
#define VMAP_VOLREC_SIZE 18 /* VolRec bytes                  */

#define VMAP_NUM_BLOCKS 0x000 /* u16 — total 1K blocks on disk */
#define VMAP_BLOCK_BASE  0x002 /* u16 — LBA of block 0          */
#define VMAP_MAGIC_OFF  0x004 /* u16 — VMAP_MAGIC              */
#define VMAP_VOLREC     0x006 /* VolRec[VOL_MAX]               */
#define VMAP_SIG        0x1FE /* u16 — BOOT_SIG                */

/* VolRec wire layout (VMAP_VOLREC_SIZE bytes each):
 *   +0  u16 run[0].start   +2  u16 run[0].count
 *   ...    run[1..VOL_MAX_RUNS-1] follow as u16 pairs (+4..+15)
 *   +16 u8  run_count      +17 u8  attr (VOL_ATTR_RW / VOL_ATTR_RO) */
#define VMAP_VR_RUN0_START  0
#define VMAP_VR_RUN0_COUNT  2
#define VMAP_VR_RUN_COUNT   16
#define VMAP_VR_ATTR        17

/* Volume header — logical LBA 0 of each volume.
 *
 * 0x000 u16 magic     — must equal DISK_MAGIC
 * 0x002 u16 ver       — VHDR_VER
 * 0x004 u16 size_kb   — volume size KB
 * 0x006 u16 root_lba  — root directory LBA
 * 0x008 u16 data_lba  — data area start LBA
 * 0x00A u16 tot_blks  — total data blocks
 * 0x1FE u16           — 0xAA55
 *
 * Block size is fixed at 2 sectors (1 KB). */

#define VHDR_MAGIC_OFF    0x00
#define VHDR_VER_OFF      0x02
#define VHDR_SIZE_KB_OFF  0x04
#define VHDR_ROOT_LBA_OFF 0x06
#define VHDR_DATA_LBA_OFF 0x08
#define VHDR_TOT_BLKS_OFF 0x0A

#define VHDR_VER          0x0001u /* volume header format version */

/* Environment slot indices */

#define ENV_SYSCALL_PTR  0   /* Pointer to syscall table */
#define ENV_RETURN_CODE  1   /* Return code of last program/command */
#define ENV_BATCH_OFFSET 2   /* Offset of batch file in CCP */
#define ENV_USER_DEFINED 3   /* User-defined environment slot */
#define ENV_SLOTS_MAX    4

#endif /* KERNEL_ABI_H */