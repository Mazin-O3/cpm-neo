/*
 * kernel/disk.h
 * CP/M Neo — block/run volume-map disk abstraction layer
 *
 * Disk layout:
 *   Sector 0 : boot sector (geometry + kernel/CCP pointers)
 *   Sector 1 : VMAP  = { u16 num_blocks, u16 block_base,
 *                        u16 magic=0x4350, VolRec[4], 0xAA55@0x1FE }
 *   Sectors 2.. : kernel (KERN_START_LBA=2), CCP, then the block grid.
 *
 * A block is a fixed run of 2 sectors (1 KB) at
 * `block_base + i*2`.  Each volume owns an ordered list of up to
 * VOL_MAX_RUNS runs (contiguous block runs); its logical space is the
 * concatenation of those runs.  A volume with ext_count==0 is unmounted.
 *
 * All byte-level layout constants (VMAP_*, VolRec, BlockRun, caps) live in
 * kernel_abi.h so the sysgen tool shares the same on-disk format.
 */

#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include "kernel_abi.h"

int      disk_init(void);                        /* 0 = OK, nonzero = failure */

uint16_t disk_blocks(void);                      /* total 1 KB blocks on disk (constant) */
uint16_t disk_block_base(void);                  /* first block after kernel/CCP         */
uint16_t disk_free_blocks(void);                 /* unallocated blocks in the grid       */

/* Sector-level I/O: lba is relative to the volume, not the physical disk.
 * Returns 0 on success, nonzero on I/O error. */
int      disk_vread(int8_t vol_id, uint32_t lba, uint8_t *buf);
int      disk_vwrite(int8_t vol_id, uint32_t lba, const uint8_t *buf);

/* Flush the disk-layer write-back cache and enforce physical persistence
 * via the BIOS barrier. Returns 0 on success, nonzero on error. */
int      disk_sync(void);

/* Volume lifecycle: mount allocates default runs, unmount frees all.
 * Returns EOK or error. */
int      disk_vmount(int8_t vol_id);             /* mount at default blocks */
int      disk_vunmount(int8_t vol_id);           /* free all blocks         */
int      disk_vextend(int8_t vol_id, uint16_t n);/* grow by n blocks        */
int      disk_vshrink(int8_t vol_id, uint16_t n);/* shrink by n blocks      */

/* Query helpers: returns 0 if the volume is unmounted. */
uint32_t disk_vsectors(int8_t vol_id);           /* capacity in sectors (0 if unmounted) */
uint8_t  disk_vruns(int8_t vol_id);              /* active runs count (0 = unmounted)  */
int      disk_vgetattr(int8_t vol_id, uint8_t *attr);
int      disk_vsetattr(int8_t vol_id, uint8_t attr);

#endif /* DISK_H */