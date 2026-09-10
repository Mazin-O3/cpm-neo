/*
 * core/kernel/disk_format.h — On-disk image and volume format
 *
 * The complete wire layout shared by the bootloader, the kernel disk
 * layer, and the sysgen image builder:
 *
 *   - Sector 0 (image identity, kernel/CCP placement, XIP flag)
 *   - VMAP, sector 1  (volume map: 1K-block run allocation per volume)
 *   - VHDR            (per-volume header at each volume's logical sector 0)
 *
 * This header contains only preprocessor defines, so it is usable from C,
 * from the GAS boot path (arch/<isa>/boot.S is preprocessed by the C
 * compiler), and from sysgen.  It pulls in config.h (the platform's
 * effective build/gen/config.h, or the sysgen host's include/config.h),
 * which also contains only preprocessor defines and is safe to include
 * from assembly too.
 */

#ifndef DISK_FORMAT_H
#define DISK_FORMAT_H

#include "config.h"

/* ── Sector-0 (S0) layout ──────────────────────────────────────────────── */

/* Sector size — The single source for sector-based I/O byte counts across
 * the bootloader (boot.S + platform bios.c), the kernel disk layer,
 * sysgen, and user programs (re-exported via abi.h).  This is an on-disk
 * format *invariant*, not a CONFIG_* tunable: the block layer (1 KB = 2
 * sectors), the volume geometry, MKDSK, and the emulator's sector DMA
 * all assume 512. */
#define DISK_SECTOR_SIZE 512

/* Sector-0 identity markers. */
#define DISK_MAGIC 0x4350 /* 'CP' — Sector-0 identity     */
#define BOOT_SIG 0xAA55   /* Standard boot sector sig     */
#define BOOT_MAGIC DISK_MAGIC
#define KERN_START_SEC 2 /* Kernel image start sector    */

#define S0_MAGIC 0x000        /* u16   — Must equal DISK_MAGIC  */
#define S0_DISK_VER 0x002     /* u16   — Disk format version    */
#define S0_DISK_SIZE_KB 0x004 /* u16   — Total disk KB          */
#define S0_KERN_LOAD 0x006    /* u32   — Kernel RAM load addr   */
#define S0_KERN_SIZE 0x00A    /* u32   — Kernel.bin raw bytes   */
#define S0_KERN_SECTORS 0x00E /* u16   — Padded on-disk sectors */
#define S0_KERN_SEC 0x010     /* u16   — Kernel start sector   */
#define S0_OS_VER 0x012       /* u16   — OS version             */
#define S0_KERN_VER 0x014     /* u16   — Kernel version         */
#define S0_CCP_VER 0x016      /* u16   — CCP version            */
#define S0_KERN_SECS 0x018    /* u16   — Kernel disk reservation */
#define S0_CCP_SEC 0x01A      /* u16   — CCP raw binary start sector */
#define S0_CCP_SIZE 0x01C     /* u16   — CCP raw binary sector count */
#define S0_PLATFORM 0x01E     /* u8[8] — Platform name, NUL-padded */
#define S0_XIP 0x026          /* u8    — 1 = XIP disk image      */
#define S0_SIG 0x1FE          /* u16   — Must equal BOOT_SIG    */

/* ── Volume-map (VMAP) format — Sector 1 ──────────────────────────────── */

/* 0x000 u16 num_blocks  — Total 1K blocks on disk
 * 0x002 u16 base_sec    — Sector of block 0
 * 0x004 u16 magic       — VMAP_MAGIC
 * 0x006 VolRec[MAX_VOLUMES] — VMAP_VOLREC_SIZE each
 * 0x1FE u16             — BOOT_SIG
 *
 * Block i occupies sectors [base_sec + i*2, +2) (1 KB = 2 sectors).
 * A volume's logical space is the concatenation of its ordered block
 * runs. Run_count == 0 means the volume is unmounted. */

#define VMAP_SEC 1         /* Volume-map sector       */
#define VMAP_MAGIC 0x4350u /* 'CP' identity           */

/* Max block runs per volume — Wire VolRec width (fixed format constant). */
#define VOL_MAX_RUNS 4

#define VMAP_VOLREC_SIZE (4 * VOL_MAX_RUNS + 2) /* VolRec bytes */

#define VMAP_NUM_BLOCKS 0x000 /* u16 — Total 1K blocks on disk */
#define VMAP_BASE_SEC 0x002   /* u16 — Sector of block 0       */
#define VMAP_MAGIC_OFF 0x004  /* u16 — VMAP_MAGIC              */
#define VMAP_VOLREC 0x006     /* VolRec[MAX_VOLUMES]               */
#define VMAP_SIG 0x1FE        /* u16 — BOOT_SIG                */

/* VolRec wire layout (VMAP_VOLREC_SIZE bytes each):
 *   run[v] occupies bytes [4v, 4v+4): u16 start, u16 count
 *   +4*VOL_MAX_RUNS        u8 run_count
 *   +4*VOL_MAX_RUNS + 1    u8 attr (VOL_ATTR_RW / VOL_ATTR_RO) */
#define VMAP_VR_RUN0_START 0
#define VMAP_VR_RUN0_COUNT 2
#define VMAP_VR_RUN_COUNT (4 * VOL_MAX_RUNS)
#define VMAP_VR_ATTR (4 * VOL_MAX_RUNS + 1)

/* ── Volume header — Logical sector 0 of each volume ──────────────────── */

/* 0x000 u16 magic     — Must equal DISK_MAGIC
 * 0x002 u16 ver       — VHDR_VER
 * 0x004 u16 size_kb   — Volume size KB
 * 0x006 u16 root_sec  — Root directory sector
 * 0x008 u16 data_sec  — Data area start sector
 * 0x00A u16 tot_blks  — Total data blocks
 * 0x1FE u16           — 0xAA55
 *
 * Block size is fixed at 2 sectors (1 KB). */

#define VHDR_MAGIC_OFF 0x00
#define VHDR_VER_OFF 0x02
#define VHDR_SIZE_KB_OFF 0x04
#define VHDR_ROOT_SEC_OFF 0x06
#define VHDR_DATA_SEC_OFF 0x08
#define VHDR_TOT_BLKS_OFF 0x0A

#define VHDR_VER 0x0001u /* Volume header format version */

#endif /* DISK_FORMAT_H */