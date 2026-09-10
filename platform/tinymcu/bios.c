/*
 * platform/tinymcu/bios.c
 * CP/M Neo - TinyMCU hardware BIOS implementation
 *
 * Pure physical I/O. No volume awareness. Translation happens in
 * kernel/disk.c. Includes mmio.h for direct register access.
 *
 * Console is the UART (polled, not interrupt-driven). Disk is a RAM
 * buffer, not a real storage peripheral. TinyMCU does have an XIP/SPI
 * flash interface now (rtl/core/tinymcu_imem_xip.vhd, driven from
 * software via sw/lib/core/xip/), but it is wired up as an
 * execute-in-place instruction source, not as a block storage backend
 * behind bios_read()/bios_write() below. The RAM disk starts empty on
 * every reset: there is no persistence across power cycles until either
 * a bios_read()/bios_write() backend on top of the XIP flash exists, or
 * until the SRAM's initial contents are baked into the bitstream the
 * same way the Boot ROM's are.
 */

#include "bios.h"
#include "mmio.h"
#include <stddef.h>

/*
 * Storage backend for bios_read()/bios_write(), selected at compile time.
 * Default: the RAM disk. TINYMCU_STORAGE_SPI is reserved for a future
 * backend built on top of the XIP flash interface's SPI transfer
 * functions (sw/lib/core/xip/tinymcu_xip.h); no such backend exists yet,
 * only the stub below. Once it does, build with
 * -DTINYMCU_STORAGE=TINYMCU_STORAGE_SPI to switch -- only one backend is
 * ever compiled in, so there's no risk of the wrong one's bios_read()/
 * bios_write() accidentally getting linked in.
 */
#define TINYMCU_STORAGE_RAMDISK 1
#define TINYMCU_STORAGE_SPI     2

#ifndef TINYMCU_STORAGE
#define TINYMCU_STORAGE TINYMCU_STORAGE_RAMDISK
#endif

#if TINYMCU_STORAGE == TINYMCU_STORAGE_RAMDISK

/*
 * RAM disk. Its own independent 128 KB SRAM block (RAMDISK_ADDR_WIDTH=15
 * in rtl/core/tinymcu_cpu.vhd), decoded at its own top-byte window
 * (0x03xx_xxxx, see tinymcu_addr_decoder.vhd's RAMDISK_TOP_BYTE) rather
 * than a sub-range of RAM_BASE. kernel/TPA (RAM_ADDR_WIDTH=14, 64 KB)
 * and the RAM disk are each an exact power of two on their own, no
 * address-range gap to handle. Update this together with
 * rtl/Makefile's RAM_ADDR_WIDTH/RAMDISK_ADDR_WIDTH if the split
 * changes.
 */
#define TINYMCU_SECTOR_SIZE     512u
#define TINYMCU_RAMDISK_BASE    0x03000000u     /* RAMDISK_TOP_BYTE (tinymcu_addr_decoder.vhd) */
#define TINYMCU_RAMDISK_SECTORS 256u            /* 256 * 512 B = 128 KB */

#elif TINYMCU_STORAGE == TINYMCU_STORAGE_SPI
#error "TINYMCU_STORAGE_SPI selected, but no SPI/SD controller exists yet (see rtl/peripherals/)"
#else
#error "unknown TINYMCU_STORAGE"
#endif

/* Console baud rate. TINYMCU_UART_BAUDRATE holds clk_i cycles per bit directly.
 */
#define TINYMCU_UART_BAUD       9600u

int bios_init(void)
{
    MMIO_W32(TINYMCU_UART_CONFIG, TINYMCU_UART_CONFIG_8N1);
    MMIO_W32(TINYMCU_UART_BAUDRATE, (TINYMCU_CLK_KHZ * 1000u) / TINYMCU_UART_BAUD);

    MMIO_W32(TINYMCU_TIMER_COUNTER, 0);
    MMIO_W32(TINYMCU_TIMER_CONFIG, TINYMCU_TIMER_CLKSEL_DIV1);
    return 0;
}

uint32_t bios_time(void)
{
    return MMIO_R32(TINYMCU_TIMER_COUNTER) / TINYMCU_CLK_KHZ;
}

void bios_conout(int c)
{
    if (c == '\n')
    {
        while (MMIO_R32(TINYMCU_UART_STATUS) & TINYMCU_UART_STATUS_TX_ACTIVE);
        MMIO_W32(TINYMCU_UART_TX_DATA, '\r');
    }

    while (MMIO_R32(TINYMCU_UART_STATUS) & TINYMCU_UART_STATUS_TX_ACTIVE);
    MMIO_W32(TINYMCU_UART_TX_DATA, (uint8_t)c);
}

int bios_constat(void)
{
    return (MMIO_R32(TINYMCU_UART_STATUS) & TINYMCU_UART_STATUS_RX_READY) ? 0xFF : 0;
}

int bios_conin(void)
{
    while (!(MMIO_R32(TINYMCU_UART_STATUS) & TINYMCU_UART_STATUS_RX_READY));
    return (int)MMIO_R32(TINYMCU_UART_RX_DATA);
}

void bios_consize(uint8_t *cw, uint8_t *ch)
{
    *cw = 80;
    *ch = 24;
}

#if TINYMCU_STORAGE == TINYMCU_STORAGE_RAMDISK

int bios_read(uint16_t lba, uint8_t *buf)
{
    if (buf == NULL || lba >= TINYMCU_RAMDISK_SECTORS)
        return -1;

    const uint8_t *src = (const uint8_t *)(uintptr_t)(TINYMCU_RAMDISK_BASE + (uint32_t)lba * TINYMCU_SECTOR_SIZE);
    for (uint32_t i = 0; i < TINYMCU_SECTOR_SIZE; i++)
        buf[i] = src[i];
    return 0;
}

int bios_write(uint16_t lba, const uint8_t *buf)
{
    if (buf == NULL || lba >= TINYMCU_RAMDISK_SECTORS)
        return -1;

    uint8_t *dst = (uint8_t *)(uintptr_t)(TINYMCU_RAMDISK_BASE + (uint32_t)lba * TINYMCU_SECTOR_SIZE);
    for (uint32_t i = 0; i < TINYMCU_SECTOR_SIZE; i++)
        dst[i] = buf[i];
    return 0;
}

int bios_sync(void)
{
    /* bios_write() above stores directly into the RAM disk's SRAM with
     * no deferred cache in between, so every accepted write is already
     * durable by the time bios_write() returns. No barrier work needed. */
    return 0;
}

#endif /* TINYMCU_STORAGE == TINYMCU_STORAGE_RAMDISK */
