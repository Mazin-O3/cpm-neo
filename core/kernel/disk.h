/*
 * core/kernel/disk.h — Block/run volume-map disk abstraction layer
 *
 * Disk layout:
 *   Sector 0 : boot sector (geometry + kernel/CCP pointers)
 *   Sector 1 : VMAP  = { u16 num_blocks, u16 base_sec,
 *                        u16 magic=0x4350, VolRec[MAX_VOLUMES],
 *                        0xAA55@0x1FE }
 *   Sectors 2.. : kernel (KERN_START_SEC=2), CCP, then the block grid.
 *
 * A block is a fixed run of 2 sectors (1 KB) at
 * `base_sec + i*2`.  Each volume owns an ordered list of up to
 * VOL_MAX_RUNS runs (contiguous block runs); its logical space is the
 * concatenation of those runs.  A volume with run_count==0 is unmounted.
 *
 * The API is split into two groups: whole-disk functions (disk_*) and
 * per-volume functions (volume_*), which take a vol_id.
 *
 * All byte-level layout constants (VMAP_*, VolRec, caps) live in
 * disk_format.h so the sysgen tool shares the same on-disk format.
 *
 * Functions return EOK (0) on success or a negative errno; the query
 * helpers that return data instead (volume_sectors, volume_run_count,
 * volume_readonly) return 0 when they cannot answer.
 */

#ifndef DISK_H
#define DISK_H

#include <stdint.h>

#include "byteorder.h"
#include "errno.h"
#include "fsctx.h"

/* Whole-disk functions */
int disk_init(void); /* Load VMAP       */
int disk_xip(void);  /* 1 = XIP image   */

/*
 * Translate a volume-relative sector index through the volume's block runs
 * into a physical disk sector.  Returns 0 on success, EINVAL for an invalid
 * volume/sector, ENOENT if the sector lies beyond the volume's end.
 */
int disk_translate(int8_t vol_id, uint16_t sec, uint16_t *phy_sec);

uint16_t disk_block_count(void); /* Total 1 KB blocks on disk (constant) */
uint16_t disk_base_sec(void);    /* Sector of block 0                    */
uint16_t disk_free_blocks(void); /* Unallocated blocks in the grid       */

/*
 * Flush the disk-layer write-back cache and enforce physical persistence
 * via the BIOS barrier.
 */
int disk_sync(void);

/* Sector-level I/O — sec is relative to the volume. */
int volume_read(int8_t vol_id, uint16_t sec, uint8_t *buf);
int volume_write(int8_t vol_id, uint16_t sec, const uint8_t *buf);

/* Volume lifecycle */
int volume_mount(int8_t vol_id); /* Mount at default blocks            */
int volume_unmount(int8_t vol_id);

/*
 * Resize a volume by delta blocks.  delta > 0 grows by delta, delta < 0
 * shrinks by |delta|, delta == 0 is a no-op.
 */
int volume_resize(int8_t vol_id, int16_t delta);

/* Volume queries */
uint32_t volume_sectors(int8_t vol_id);   /* Capacity in sectors (0=unmnt) */
uint8_t  volume_run_count(int8_t vol_id); /* Active runs count             */
int      volume_getattr(int8_t vol_id, uint8_t *attr);
int      volume_setattr(int8_t vol_id, uint8_t attr);
int      volume_readonly(int8_t vol_id);

#endif /* DISK_H */
