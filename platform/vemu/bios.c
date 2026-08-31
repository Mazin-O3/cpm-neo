/*
 * platform/vemu/bios.c
 * CP/M Neo — Vemu hardware BIOS implementation
 *
 * Pure physical I/O.  No volume awareness — translation
 * happens in kernel/disk.c.  Includes mmio.h for direct
 * register access.
 */

#include "bios.h"
#include "mmio.h"
#include <stddef.h>

/* DMA-based sector read/write */

static int dma_transfer(uint16_t dst, uint16_t src, uint32_t count, uint8_t flags)
{
    uint8_t width = flags & DMA_FLAG_WIDTH_MASK;
    uint8_t step = (width == DMA_FLAG_WIDTH_16)   ? DMA_CSTR_WIDTH_16
                   : (width == DMA_FLAG_WIDTH_32) ? DMA_CSTR_WIDTH_32
                                                  : DMA_CSTR_WIDTH_8;

    uint8_t cfg = step | (flags & DMA_FLAG_SRC_INC ? DMA_CSTR_SRC_INC : 0) |
                  (flags & DMA_FLAG_DST_INC ? DMA_CSTR_DST_INC : 0) |
                  (flags & DMA_FLAG_STREAM ? DMA_CSTR_STREAM : 0);

    MMIO_W16(DMA_SAR, src);
    MMIO_W16(DMA_DAR, dst);
    MMIO_W16(DMA_WCR, count);
    MMIO_W8(DMA_CSTR, cfg | DMA_CSTR_START);
    return (MMIO_R8(DMA_CSTR) & DMA_CSTR_RUNNING) ? -1 : 0;
}

static uint16_t g_time_prev;
static uint32_t g_time_acc;

int bios_init(void)
{
    MMIO_W8(TIMER_CSTR, TIMER_PRESCALE_1 | TIMER_CST_EN);
    MMIO_W16(TIMER_CNTR, 0);
    g_time_prev = 0;
    g_time_acc = 0;
    return 0;
}

uint32_t bios_time(void)
{
    uint16_t now = MMIO_R16(TIMER_CNTR);

    g_time_acc += (uint16_t)(now - g_time_prev);

    g_time_prev = now;

    uint16_t khz = MMIO_R16(CLK_KHZ);

    if (khz == 0)
        khz = 1;

    return (uint32_t)g_time_acc / khz;
}

void bios_conout(int c)
{
    MMIO_W8(DSP_DATA, c);
}

int bios_constat(void)
{
    return MMIO_R32(KBD_STAT) ? 0xFF : 0;
}

int bios_conin(void)
{
    int c;

    while ((c = MMIO_R32(KBD_DATA)) == 0)
        ;
    return c;
}

int bios_read(uint16_t lba, uint8_t *buf)
{
    if (buf == NULL)
        return -1;

    MMIO_W16(DISK_SECTOR, lba | DISK_CFG_READ);
    return dma_transfer((uint16_t)(uintptr_t)buf, (uint16_t)DISK_BUFFER, DISK_SECTOR_SIZE,
                        DMA_DISK_RD);
}

void bios_consize(uint8_t *cw, uint8_t *ch)
{
    *cw = 80;
    *ch = 24;
}

int bios_write(uint16_t lba, const uint8_t *buf)
{
    int rc = dma_transfer((uint16_t)DISK_BUFFER, (uint16_t)(uintptr_t)buf, DISK_SECTOR_SIZE,
                          DMA_DISK_WR);

    if (rc != 0)
        return rc;

    MMIO_W16(DISK_SECTOR, lba | DISK_CFG_WRITE);
    return 0;
}

int bios_sync(void)
{
    /* VEMU sector writes reach the emulated disk immediately and are
     * persisted by the host shell (dirty-flag -> IndexedDB). There is no
     * deferred platform cache, so no barrier work is required here. */
    return 0;
}
