/*
 * bios/mmio.h
 * CP/M Neo — Memory-mapped I/O register definitions
 *
 * Shared by the kernel, libc, and BIOS. Never include from user programs.
 */

#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

/* MMIO access macros */

/*
 * All addresses are passed as integers (uint16_t peripheral offsets).
 * Casting an integer directly to a pointer of a different width triggers
 * -Wint-to-pointer-cast on RV32 because pointers are 32 bits wide.
 * The correct idiom is to widen to uintptr_t first, which matches the
 * pointer width on every target, and then cast to the access type.
 */
#define MMIO_W32(addr, val)                                                                        \
    __asm__ volatile("sw %1, 0(%0)" : : "r"((uintptr_t)(addr)), "r"((uint32_t)(val)) : "memory")

#define MMIO_W16(addr, val)                                                                        \
    __asm__ volatile("sh %1, 0(%0)" : : "r"((uintptr_t)(addr)), "r"((uint16_t)(val)) : "memory")

#define MMIO_W8(addr, val)                                                                         \
    __asm__ volatile("sb %1, 0(%0)" : : "r"((uintptr_t)(addr)), "r"((uint8_t)(val)) : "memory")

#define MMIO_R32(addr)                                                                             \
    ({                                                                                             \
        uintptr_t _a = (uintptr_t)(addr);                                                          \
        uint32_t  _v;                                                                              \
        __asm__ volatile("lw %0, 0(%1)" : "=r"(_v) : "r"(_a) : "memory");                          \
        _v;                                                                                        \
    })

#define MMIO_R16(addr)                                                                             \
    ({                                                                                             \
        uintptr_t _a = (uintptr_t)(addr);                                                          \
        uint16_t  _v;                                                                              \
        __asm__ volatile("lhu %0, 0(%1)" : "=r"(_v) : "r"(_a) : "memory");                         \
        _v;                                                                                        \
    })

#define MMIO_R8(addr)                                                                              \
    ({                                                                                             \
        uintptr_t _a = (uintptr_t)(addr);                                                          \
        uint8_t   _v;                                                                              \
        __asm__ volatile("lbu %0, 0(%1)" : "=r"(_v) : "r"(_a) : "memory");                         \
        _v;                                                                                        \
    })

/* Peripheral base addresses */

/*
 * The I/O window base is exported by the linker as __io_base, supplied via
 * --defsym from the platform config (platform/<name>/config.sh, IO_BASE).
 * Every translation unit that includes mmio.h must be linked with a
 * definition of __io_base (kernel: linker_kernel.ld, bootloader:
 * linker_boot.ld).
 */
extern char __io_base[];
#define IO_BASE ((uintptr_t)__io_base)

/*
 * Every register lives in its own 4-byte-aligned word.  The CPU load
 * path always reads full 32-bit words, so a register with a
 * side-effectful read (KBD_DATA pops, DISK_BUFFER advances) must be
 * isolated from every other register.
 *
 *  IO+0x00  DISK_BUFFER   IO+0x18  TIMER_CSTR
 *  IO+0x04  DISK_SECTOR   IO+0x1C  TIMER_CNTR
 *  IO+0x08  KBD_DATA      IO+0x20  DMA_SAR
 *  IO+0x0C  KBD_STATUS    IO+0x24  DMA_DAR
 *  IO+0x10  DSP_DATA      IO+0x28  DMA_WCR
 *  IO+0x14  CLK_KHZ       IO+0x2C  DMA_CSTR
 */

#define DISK_BASE (IO_BASE + 0x00)
#define KBD_BASE (IO_BASE + 0x08)
#define DSP_BASE (IO_BASE + 0x10)
#define CLK_KHZ_BASE (IO_BASE + 0x14)
#define TIMER_BASE (IO_BASE + 0x18)
#define DMA_BASE (IO_BASE + 0x20)

/*
 * Single-channel DMA. Register map:
 *   DMA_SAR  — Source address (16-bit)
 *   DMA_DAR  — Destination address (16-bit)
 *   DMA_WCR  — Word count (16-bit)
 *   DMA_CSTR — Control (write) / Status (read) (8-bit)
 */

#define DMA_SAR (DMA_BASE + 0x00)  /* IO+0x20 */
#define DMA_DAR (DMA_BASE + 0x04)  /* IO+0x24 */
#define DMA_WCR (DMA_BASE + 0x08)  /* IO+0x28 */
#define DMA_CSTR (DMA_BASE + 0x0C) /* IO+0x2C */

/*
 * DMA_CSTR — 8-bit control/status register
 *
 * Write layout:
 *   [0]   START    write 1 to toggle start/stop
 *   [1]   STREAM   1=stream mode (bus-lock)  0=normal mode
 *   [2]   SRC_INC  auto-increment source address
 *   [3]   DST_INC  auto-increment destination address
 *   [5:4] STEP     transfer width: 00=8b  01=16b  10=32b
 *   [7:6] ignored
 *
 * Read layout:
 *   [0]   RUNNING  1=channel busy (transfer in progress)
 *   [1]   STREAM   readback of latched stream bit
 *   [2]   SRC_INC  readback
 *   [3]   DST_INC  readback
 *   [5:4] STEP     readback
 *   [7:6] 0
 */

#define DMA_CSTR_START (1u << 0)
#define DMA_CSTR_STREAM (1u << 1)
#define DMA_CSTR_SRC_INC (1u << 2)
#define DMA_CSTR_DST_INC (1u << 3)
#define DMA_CSTR_WIDTH_8 (0u << 4)
#define DMA_CSTR_WIDTH_16 (1u << 4)
#define DMA_CSTR_WIDTH_32 (2u << 4)
#define DMA_CSTR_WIDTH_MASK (3u << 4)

/* Read layout only: bit 0 reads back as the channel-busy status. */
#define DMA_CSTR_RUNNING (1u << 0)

#define DMA_CSTR_START_STREAM (DMA_CSTR_START | DMA_CSTR_STREAM)

/*
 * DMA transfer flags — Used by bios_dma_transfer() / SYS_DMA.
 * These are the public API flags, independent of register layout.
 * bios_dma_transfer() packs them into DMA_CSTR format.
 *
 * Bit layout:
 *   [1:0] width    00=8-bit  01=16-bit  10=32-bit
 *   [2]   src_inc  auto-increment source address
 *   [3]   dst_inc  auto-increment destination address
 *   [4]   stream   1=stream mode (bus-lock)  0=normal mode
 */

#define DMA_FLAG_WIDTH_8 (0u << 0)
#define DMA_FLAG_WIDTH_16 (1u << 0)
#define DMA_FLAG_WIDTH_32 (2u << 0)
#define DMA_FLAG_WIDTH_MASK (3u << 0)
#define DMA_FLAG_SRC_INC (1u << 2)
#define DMA_FLAG_DST_INC (1u << 3)
#define DMA_FLAG_STREAM (1u << 4)

#define DMA_MEMCPY (DMA_FLAG_SRC_INC | DMA_FLAG_DST_INC | DMA_FLAG_STREAM)
#define DMA_DISK_RD (DMA_FLAG_DST_INC | DMA_FLAG_STREAM)
#define DMA_DISK_WR (DMA_FLAG_SRC_INC | DMA_FLAG_STREAM)

/* Timer registers */

#define TIMER_CSTR (TIMER_BASE + 0x00) /* IO+0x18 */
#define TIMER_CNTR (TIMER_BASE + 0x04) /* IO+0x1C */

#define TIMER_CST_EN (1u << 0)
#define TIMER_CST_MOD (1u << 1)
#define TIMER_CST_OVF (1u << 2)
#define TIMER_PRESCALE_1 (0u << 4)
#define TIMER_PRESCALE_8 (1u << 4)
#define TIMER_PRESCALE_64 (2u << 4)
#define TIMER_PRESCALE_256 (3u << 4)

/* Keyboard register */

#define KBD_DATA (KBD_BASE + 0x00) /* IO+0x08: read pops next key */
#define KBD_STAT (KBD_BASE + 0x04) /* IO+0x0C: 1 = key ready, no consume */

/* Display register */

#define DSP_DATA (DSP_BASE + 0x00)

/* Disk registers */

#define DISK_BUFFER (DISK_BASE + 0x00) /* IO+0x00 */
#define DISK_SECTOR (DISK_BASE + 0x04) /* IO+0x04 */
#define DISK_CFG_READ (0x0000)
#define DISK_CFG_WRITE (0x8000)

/* Clock register */

#define CLK_KHZ (CLK_KHZ_BASE + 0x00)

#endif /* MMIO_H */
