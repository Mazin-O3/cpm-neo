/*
 * sysgen/src/bios.c
 * CP/M Neo SYSGEN — Host BIOS adapter
 */

#include "bios.h"
#include "sysgen.h"

#include <stdio.h>
#include <string.h>

static uint8_t *g_disk;
static uint32_t g_disk_size;

int bios_init(void)
{
    return 0;
}

void bios_conout(int c)
{
    fputc(c, stderr);
}

int bios_conin(void)
{
    return 0;
}

int bios_constat(void)
{
    return 0;
}

int bios_read(uint16_t sec, uint8_t *buf)
{
    uint32_t off = (uint32_t)sec * DISK_SECTOR_SIZE;

    if (off + DISK_SECTOR_SIZE > g_disk_size)
        return -1;

    memcpy(buf, g_disk + off, DISK_SECTOR_SIZE);
    return 0;
}

int bios_write(uint16_t sec, const uint8_t *buf)
{
    uint32_t off = (uint32_t)sec * DISK_SECTOR_SIZE;

    if (off + DISK_SECTOR_SIZE > g_disk_size)
        return -1;

    memcpy(g_disk + off, buf, DISK_SECTOR_SIZE);
    return 0;
}

int bios_sync(void)
{
    /* The in-memory host disk image is already committed; the caller is
     * responsible for writing it out to a file. No barrier work needed. */
    return 0;
}

void bios_consize(uint8_t *cw, uint8_t *ch)
{
    (void)cw;
    (void)ch;
}

uint32_t bios_time(void)
{
    return 0;
}

void sysgen_set_disk(uint8_t *disk, uint32_t size)
{
    g_disk = disk;
    g_disk_size = size;
}

uint8_t *sysgen_disk(void)
{
    return g_disk;
}

uint32_t sysgen_disk_size(void)
{
    return g_disk_size;
}
