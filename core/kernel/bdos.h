/*
 * core/kernel/bdos.h — BDOS filesystem layer
 *
 * Extent-based filesystem.  A BDOS block is two disk sectors
 * (1 KB); eight blocks form one extent; the allocation bitmap supports
 * up to BD_VOL_MAX_BLOCKS blocks per volume.
 *
 * Tunables come from config.h (the platform's effective values, via the
 * generated build/gen/config.h; the sysgen host resolves its own ceiling
 * include/config.h).  User-facing ABI types come from abi.h.
 */

#ifndef BDOS_H
#define BDOS_H

#include "abi.h"
#include "config.h"
#include "errno.h"
#include <stdint.h>

#define BD_MAX_FCBS CONFIG_FCB_MAX
#define BD_DISK_MAX_SECS 65535

/*
 * BDOS allocation geometry.
 *
 * A BDOS block consists of two disk sectors.  Eight blocks form one
 * extent, and the allocation bitmap supports up to BD_VOL_MAX_BLOCKS
 * blocks per volume.  CONFIG_DISK_SIZE is the per-volume disk-size cap in
 * KB (1 KB per block), so the bitmap is ceil(CONFIG_DISK_SIZE/8) bytes
 * (one bit per block) and the volume cap is CONFIG_DISK_SIZE blocks.
 */
#define BD_BLOCK_SECS 2
#define BD_BLOCK_BYTES (BD_BLOCK_SECS * DISK_SECTOR_SIZE)
#define BD_BLOCKS_PER_EXTENT 8
#define BD_BLOCK_MAP_BYTES ((CONFIG_DISK_SIZE + 7) / 8) /* Ceil: one bit per 1K block */
#define BD_VOL_MAX_BLOCKS CONFIG_DISK_SIZE        /* Per-volume block cap */

#define BD_ENTRY_SIZE 32
#define BD_ROOT_ENTRIES 256

/* The extent index is a uint8_t on disk, so at most 256 extents (2 MB) are
 * representable per file; the shared 256-entry root directory bounds
 * this further. */
#define BD_MAX_EXTENTS 256

#define BD_ENTRIES_PER_SEC (DISK_SECTOR_SIZE / BD_ENTRY_SIZE)
#define BD_EXTENT_BYTES (BD_BLOCKS_PER_EXTENT * BD_BLOCK_BYTES)

/* Per-volume metadata layout: header sector 0, then the root directory.
 * BD_DATA_START is the first sector of a volume's data blocks. */
#define BD_HEADER_SECS 1
#define BD_ROOT_SECS (BD_ROOT_ENTRIES * BD_ENTRY_SIZE / DISK_SECTOR_SIZE)
#define BD_DATA_START (BD_HEADER_SECS + BD_ROOT_SECS)

/* Data block 0 of every volume is unusable: directory extent lists encode
 * an absent slot as 0, so the allocator permanently reserves it. */
#define BD_RESERVED_BLOCKS 1

/* Minimum volume size: header + root, plus the reserved block and at
 * least one usable data block. */
#define BD_MIN_VOL_SECS (BD_DATA_START + (BD_RESERVED_BLOCKS + 1) * BD_BLOCK_SECS)

#define BD_DIR_ATTR 11
#define BD_DIR_USER 12
#define BD_DIR_EXTENT_IDX 13
#define BD_DIR_EXTENT_BYTES 14
#define BD_DIR_BLOCKS 16

#define BD_ENTRY_EMPTY 0x00
#define BD_ENTRY_DELETED 0xE5

#define BD_USER_INVALID 0xFF
#define BD_HEADER_SECS 1
#define BD_BITS_PER_BYTE 8
#define BD_BITMAP_FULL UINT8_MAX
#define BD_RESERVED_BLOCK 0
#define BD_SECTORS_PER_KB (1024 / DISK_SECTOR_SIZE)
#define DIR_SCAN_STOP 1

/*
 * Bind an existing formatted volume.  Closes stale FCBs from any
 * previous bind on the same vol_id, then rescans the alloc map.
 */
int bd_bind(int8_t vol_id);

/*
 * Format and bind a fresh volume (SET MT).  Requires a prior
 * volume_mount() call.
 */
int bd_mount(int8_t vol_id);

/*
 * Resize a mounted volume by delta blocks.  delta > 0 grows, delta < 0
 * shrinks (by |delta|), delta == 0 is a no-op.  Grow fails with EVOLRO
 * on read-only volumes and ENOSPC at BD_VOL_MAX_BLOCKS.  Shrink returns
 * EPERM if any target blocks are allocated; EINVAL if the result would
 * be below BD_MIN_VOL_SECS.
 */
int bd_resize(int8_t vol_id, int16_t delta);

/* Unbind a volume.  Returns EPERM if the volume still has allocated
 * data blocks (not empty). */
int bd_unbind(int8_t vol_id);

/* Flush the write-back cache and refresh free-block hints for
 * idle volumes (those with no open writable files). */
int bd_sync(void);

/* Open an existing file.  Returns a non-negative fd on success,
 * or EFILERO/EPERM/ENOVOL/ENFILE on error. */
int bd_open(const char *name83, FsContext ctx, uint8_t writable);

/* Create a new empty file.  Returns EEXIST if the name already
 * exists, EDIRFULL if the root directory is full. */
int bd_create(const char *name83, FsContext ctx);

/* Read up to len bytes at the current position.  May return fewer
 * bytes than requested at EOF or on extent boundary. */
int bd_read(int fd, uint8_t *buf, uint16_t len);

/* Write up to len bytes.  Returns bytes written (may be short at
 * ENOSPC), or a negative error code. */
int bd_write(int fd, const uint8_t *buf, uint16_t len);

/* Close a file descriptor, flushing any dirty data. */
int bd_close(int fd);

/* Return the total size in bytes of the open file. */
uint32_t bd_size(int fd);

/* Delete a file.  Returns EPERM if the file is read-only. */
int bd_delete(const char *name83, FsContext ctx);

/* Rename a file.  Returns EEXIST if new83 is already taken.
 * No data blocks are moved. */
int bd_rename(const char *old83, const char *new83, FsContext ctx);

/* Search the directory for files matching a wildcard pattern.
 * Returns a 1-based directory index on match, or ENOENT.
 * Pass start_pos to resume a previous scan. */
int bd_find(const char *pat, FsContext ctx, FileInfo *out, uint16_t start_pos);

/* Read volume metadata (total sectors, free blocks, mount state). */
int bd_vstat(int8_t vol_id, VolStat *stat);

/* Set the file position for the next read or write. */
int bd_seek(int fd, uint32_t offset);

/* Set attributes on all extents of a file.  Returns ENOENT if the
 * file does not exist. */
int bd_fsetattr(const char *name83, FsContext ctx, uint8_t attrib);

/* Set the attribute byte on a mounted volume. */
int bd_vsetattr(int8_t vol_id, uint8_t attr);

#endif