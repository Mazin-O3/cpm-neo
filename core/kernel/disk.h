/*
 * kernel/disk.h
 * CP/M Neo — block/run volume-map disk abstraction layer
 *
 * Disk layout:
 *   Sector 0 : boot sector (geometry + kernel/CCP pointers)
 *   Sector 1 : VMAP  = { u16 num_blocks, u16 base_sec,
 *                        u16 magic=0x4350, VolRec[4], 0xAA55@0x1FE }
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
 * All byte-level layout constants (VMAP_*, VolRec, BlockRun, caps) live in
 * kernel_abi.h so the sysgen tool shares the same on-disk format.
 */

#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include "kernel_abi.h"

int      disk_init(void);                        /* 0 = OK, nonzero = failure */
int      disk_xip(void);                         /* 1 = XIP disk image         */

/* Translate a volume-relative sector index through the volume's block runs
 * into a physical disk sector.  Returns 0 on success, -1 on error. */
int      disk_translate(int8_t vol_id, uint16_t sec, uint16_t *phy_sec);

uint16_t disk_block_count(void);                 /* total 1 KB blocks on disk (constant) */
uint16_t disk_base_sec(void);                    /* sector of block 0                   */
uint16_t disk_free_blocks(void);                 /* unallocated blocks in the grid       */

/* Flush the disk-layer write-back cache and enforce physical persistence
 * via the BIOS barrier. Returns 0 on success, nonzero on error. */
int      disk_sync(void);

/* Sector-level I/O: sec is relative to the volume.
 * Returns 0 on success, nonzero on I/O error. */
int      volume_read(int8_t vol_id, uint16_t sec, uint8_t *buf);
int      volume_write(int8_t vol_id, uint16_t sec, const uint8_t *buf);

/* Volume lifecycle: mount allocates default runs, unmount frees all.
 * Returns EOK or error. */
int      volume_mount(int8_t vol_id);            /* mount at default blocks */

/* Unmount a volume: frees all its blocks. Returns EOK or error. */
int      volume_unmount(int8_t vol_id);

/* Resize a volume by delta blocks. delta > 0 grows by delta, delta < 0
 * shrinks by |delta|, delta == 0 is a no-op. Returns EOK or error. */
int      volume_resize(int8_t vol_id, int16_t delta);

/* Query helpers: returns 0 if the volume is unmounted. */
uint32_t volume_sectors(int8_t vol_id);          /* capacity in sectors (0 if unmounted) */
uint8_t  volume_run_count(int8_t vol_id);        /* active runs count (0 = unmounted)  */
int      volume_getattr(int8_t vol_id, uint8_t *attr);
int      volume_setattr(int8_t vol_id, uint8_t attr);

#endif /* DISK_H */