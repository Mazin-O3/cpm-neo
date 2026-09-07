/*
 * sysgen/mkdisk.c
 * CP/M Neo SYSGEN — Disk image assembly
 *
 * Writes sector 0 (geometry), the VMAP at sector 1 (block grid + VolRec[4]),
 * the kernel image (with BOOT_MAGIC trailer), the raw CCP binary, and a
 * formatted filesystem for every volume (header + empty root).  The block
 * grid (1 KB blocks) is divided equally between the four volumes (A:-D:), so
 * all of them are mounted and usable at boot; any leftover blocks go to the
 * earliest volumes.  Volumes can later be resized at runtime with SET RZ +N,
 * or unmounted with SET UM.
 */

#include "bdos.h"
#include "sysgen.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t min_viable_blocks(void)
{
    return (BD_MIN_VOL_SECS + BD_BLOCK_SECS - 1) / BD_BLOCK_SECS;
}

/* Sector count for the kernel image.  The raw image is padded up to a whole
 * sector.  On non-XIP disks one more sector is reserved when the padding is
 * under 4 bytes, to carry the BOOT_MAGIC trailer the RAM-loading bootloader
 * validates.  On XIP disks the bootloader jumps straight into the kernel from
 * flash and never validates the magic, so no trailer sector is taken — The
 * CCP then starts immediately after the kernel's own footprint. */
static uint32_t kernel_sectors(uint32_t kern_size, int xip)
{
    uint32_t num_kern_sects = (kern_size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;

    if (!xip)
    {
        uint32_t padding = num_kern_sects * DISK_SECTOR_SIZE - kern_size;

        if (padding < 4)
            num_kern_sects++;
    }

    return num_kern_sects;
}

static uint32_t ccp_sectors(uint32_t ccp_size)
{
    return (ccp_size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;
}

/* Number of sectors reserved for the kernel + CCP images at the start of
 * the data area.  On non-XIP disks the kernel image is padded to a sector
 * boundary with a guaranteed 4-byte BOOT_MAGIC trailer slot; on XIP disks
 * the CCP must start exactly at the kernel's sector-aligned end so its
 * on-disk address matches its link origin. */
static uint32_t reserve_kernel_ccp(uint32_t kern_size, uint32_t ccp_size, int xip)
{
    return kernel_sectors(kern_size, xip) + ccp_sectors(ccp_size);
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * elf32_symbol — Look up a symbol value in a 32-bit ELF binary.
 * Returns 0 on success (value written to *value), -1 on failure.
 * Used by sysgen to find __kernel_base from the kernel ELF.
 */
int elf32_symbol(const uint8_t *e, size_t n, const char *name, uint32_t *value)
{
    if (!e || n < 52)
        return -1;

    if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F')
        return -1;

    if (e[4] != 1) /* ELFCLASS32 */
        return -1;

    uint32_t shoff = read32(e + 32);
    uint16_t shentsize = read16(e + 46);
    uint16_t shnum = read16(e + 48);

    if (shentsize != 40 || shnum == 0 || shoff == 0)
        return -1;

    if (shoff + (uint32_t)shnum * 40 > n)
        return -1;

    uint32_t sym_off = 0, sym_size = 0, sym_entsize = 0;
    uint32_t str_off = 0, str_size = 0;

    for (uint16_t i = 0; i < shnum; i++)
    {
        const uint8_t *sh = e + shoff + (uint32_t)i * 40;

        if (read32(sh + 4) == 2) /* SHT_SYMTAB */
        {
            sym_off = read32(sh + 16);
            sym_size = read32(sh + 20);
            sym_entsize = read32(sh + 36);
            uint16_t link = read16(sh + 24);

            if (link < shnum)
            {
                const uint8_t *st = e + shoff + (uint32_t)link * 40;
                str_off = read32(st + 16);
                str_size = read32(st + 20);
            }

            break;
        }
    }

    if (sym_entsize == 0)
        return -1;

    for (uint32_t i = 0; i + sym_entsize <= sym_size; i += sym_entsize)
    {
        const uint8_t *sym = e + sym_off + i;
        uint32_t st_name = read32(sym);
        uint32_t st_value = read32(sym + 4);

        if (st_name < str_size)
        {
            const char *nm = (const char *)(e + str_off + st_name);

            if (strcmp(nm, name) == 0)
            {
                *value = st_value;
                return 0;
            }
        }
    }

    return -1;
}

/*
 * to_name83 — Convert a filename string to padded 8.3 format.
 * If no extension is present, ".COM" is assumed.
 *
 * Mirrors the kernel's make_name83() (core/kernel/kernel.c) for the
 * shared 8.3 padding rules; the two must stay in sync.  The extra
 * host-side behavior here is: trailing whitespace trim, and defaulting
 * a missing extension to COM (kernel leaves it space-padded).
 */
void to_name83(const char *src, char *out83)
{
    char tmp[64];
    int n = 0;

    while (src[n] && n < 63)
    {
        char c = src[n];
        tmp[n] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        n++;
    }

    tmp[n] = '\0';

    while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '\t'))
        tmp[--n] = '\0';

    char base[NAME83_BASE], ext[NAME83_EXT];
    memset(base, ' ', NAME83_BASE);
    memset(ext, ' ', NAME83_EXT);

    const char *dot = strchr(tmp, '.');

    if (dot)
    {
        int b = (int)(dot - tmp);

        if (b > NAME83_BASE)
            b = NAME83_BASE;
        memcpy(base, tmp, (size_t)b);
        int e = 0;
        const char *p = dot + 1;

        while (e < NAME83_EXT && *p)
            ext[e++] = *p++;
    }
    else
    {
        int b = n;

        if (b > NAME83_BASE)
            b = NAME83_BASE;
        memcpy(base, tmp, (size_t)b);
        memcpy(ext, "COM", 3);
    }

    memcpy(out83, base, NAME83_BASE);
    memcpy(out83 + NAME83_BASE, ext, NAME83_EXT);
}

int read_file(const char *path, uint8_t **out, uint32_t *out_len)
{
    FILE *f = fopen(path, "rb");

    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);

    if (sz < 0 || (unsigned long)sz > (unsigned long)UINT32_MAX)
    {
        fclose(f);
        return -1;
    }

    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((sz > 0) ? (size_t)sz : 1);

    if (!buf)
    {
        fclose(f);
        return -1;
    }

    size_t r = fread(buf, 1, (size_t)sz, f);

    if (r != (size_t)sz)
    {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);
    *out = buf;
    *out_len = (uint32_t)sz;
    return 0;
}

int write_file(const char *path, const uint8_t *data, uint32_t len)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return -1;

    size_t w = fwrite(data, 1, (size_t)len, f);
    int rc = fclose(f);

    if (w != (size_t)len)
        return -1;

    return rc;
}

/*
 * mkdisk_build — Assemble a complete disk image in memory.
 * Writes sector 0 (geometry), the VMAP at sector 1, the kernel image
 * (with BOOT_MAGIC trailer), the raw CCP binary, and a formatted
 * filesystem header + empty root for every volume.
 *
 * Returns the number of reserved sectors (kernel + CCP) on success,
 * or -1 on error (disk too small, invalid parameters, OOM).
 */
int mkdisk_build(uint32_t size_kb, const uint8_t *kern, uint32_t kern_size, const uint8_t *ccp,
                 uint32_t ccp_size, uint32_t kern_load, uint16_t os_ver, uint16_t kern_ver,
                 uint16_t ccp_ver, const char *platform, int xip)
{
    if (!kern || kern_size == 0 || size_kb == 0)
        return -1;

    uint32_t total_secs = size_kb * 2;

    uint32_t reserved = reserve_kernel_ccp(kern_size, ccp_size, xip);
    uint32_t num_kern_sects = kernel_sectors(kern_size, xip);
    uint32_t num_ccp_sects = ccp_sectors(ccp_size);

    if (KERN_START_SEC + reserved >= total_secs)
        return -1;

    uint16_t base_sec = KERN_START_SEC + reserved;
    uint32_t num_blocks = (total_secs - base_sec) / BD_BLOCK_SECS;

    if (num_blocks == 0)
        return -1;

    uint8_t *disk = (uint8_t *)calloc(total_secs * DISK_SECTOR_SIZE, 1);

    if (!disk)
        return -1;

    sysgen_set_disk(disk, total_secs * DISK_SECTOR_SIZE);

    /* ── Sector 0 ─────────────────────────────────────────── */
    /* S0_DISK_SIZE_KB / S0_KERN_SIZE / S0_KERN_SECS are documented
     * on-disk metadata (docs/disk-format.md) reserved for host tools;
     * the bootloader and kernel read only the fields below. */
    write16(disk + S0_MAGIC, DISK_MAGIC);
    write16(disk + S0_DISK_SIZE_KB, (uint16_t)size_kb);
    write32(disk + S0_KERN_LOAD, kern_load);
    write32(disk + S0_KERN_SIZE, kern_size);
    write16(disk + S0_KERN_SECTORS, num_kern_sects);
    write16(disk + S0_KERN_SEC, KERN_START_SEC);
    write16(disk + S0_OS_VER, os_ver);
    write16(disk + S0_KERN_VER, kern_ver);
    write16(disk + S0_CCP_VER, ccp_ver);
    write16(disk + S0_KERN_SECS, reserved);
    write16(disk + S0_CCP_SEC, KERN_START_SEC + num_kern_sects);
    write16(disk + S0_CCP_SIZE, num_ccp_sects);

    if (platform)
        memcpy(disk + S0_PLATFORM, platform, 8);

    disk[S0_XIP] = (uint8_t)(xip ? 1 : 0);

    write16(disk + S0_SIG, BOOT_SIG);

    /* ── Kernel image (padded; BOOT_MAGIC trailer on non-XIP only) ── */
    uint32_t kern_sect_bytes = num_kern_sects * DISK_SECTOR_SIZE;
    memcpy(disk + KERN_START_SEC * DISK_SECTOR_SIZE, kern, kern_size);

    if (!xip)
        write32(disk + KERN_START_SEC * DISK_SECTOR_SIZE + kern_sect_bytes - 4, BOOT_MAGIC);

    /* ── CCP raw binary ────────────────────────────────────── */

    if (ccp_size > 0)
    {
        uint16_t csec = KERN_START_SEC + num_kern_sects;
        memcpy(disk + csec * DISK_SECTOR_SIZE, ccp, ccp_size);
    }

    /* ── VMAP @ sector 1: geometry + VolRec[4] ─────────────── */
    /* Equal division ensures all volumes are mountable at boot;
     * the remainder is distributed to earliest volumes. Each volume's
     * share is further clamped to BD_VOL_MAX_BLOCKS -- the filesystem
     * layer's addressing limit for a single volume (see bd_bind /
     * bd_resize in bdos.c). Any blocks beyond that per volume are
     * deliberately left OUT of every volume's runs, so they stay
     * free in the block grid rather than being locked away in a
     * run no volume can ever use. That free pool is what lets a
     * volume span up to BD_VOL_MAX_BLOCKS blocks -- including nearly
     * the entire disk -- if it's later grown with SET RZ +N after the
     * other volumes are shrunk or unmounted. */
    uint32_t base = num_blocks / MAX_VOLUMES;
    uint32_t rem = num_blocks % MAX_VOLUMES;
    uint32_t min_blocks = min_viable_blocks();

    if (base < min_blocks)
        return -1; /* Disk too small to give every volume a viable block count */

    uint8_t *vmap = disk + VMAP_SEC * DISK_SECTOR_SIZE;
    write16(vmap + VMAP_NUM_BLOCKS, (uint16_t)num_blocks);
    write16(vmap + VMAP_BASE_SEC, base_sec);
    write16(vmap + VMAP_MAGIC_OFF, VMAP_MAGIC);

    for (uint32_t v = 0; v < MAX_VOLUMES; v++)
    {
        uint32_t start = v * base + (v < rem ? v : rem);
        uint32_t count = base + (v < rem ? 1 : 0);

        /* Reserve at most BD_VOL_MAX_BLOCKS at the disk layer for this
         * volume. The remainder of its equal share, if any, is not claimed by
         * any run and stays free in the grid. */

        if (count > BD_VOL_MAX_BLOCKS)
            count = BD_VOL_MAX_BLOCKS;

        uint32_t vr = VMAP_VOLREC + v * VMAP_VOLREC_SIZE;

        write16(vmap + vr + VMAP_VR_RUN0_START, (uint16_t)start);
        write16(vmap + vr + VMAP_VR_RUN0_COUNT, (uint16_t)count);
        vmap[vr + VMAP_VR_RUN_COUNT] = 1;      /* Run count */
        vmap[vr + VMAP_VR_ATTR] = VOL_ATTR_RW; /* Attr */

        /* ── Formatted filesystem: header + (already-zeroed) empty root ── */
        uint16_t v_secs = (uint16_t)(count * BD_BLOCK_SECS);
        uint16_t num_data = (uint16_t)((v_secs - BD_DATA_START) / BD_BLOCK_SECS);
        /* Count is already <= BD_VOL_MAX_BLOCKS above, and num_data <=
         * count after subtracting the header/root overhead, so this can
         * no longer fire -- kept as a defensive backstop only. */

        if (num_data > BD_VOL_MAX_BLOCKS)
            num_data = BD_VOL_MAX_BLOCKS;

        uint8_t *hdr = disk + (base_sec + start * BD_BLOCK_SECS) * DISK_SECTOR_SIZE;
        write16(hdr, DISK_MAGIC);
        write16(hdr + VHDR_VER_OFF, VHDR_VER);
        write16(hdr + VHDR_SIZE_KB_OFF, (uint16_t)(v_secs / 2));
        write16(hdr + VHDR_ROOT_SEC_OFF, 1);
        write16(hdr + VHDR_DATA_SEC_OFF, BD_DATA_START);
        write16(hdr + VHDR_TOT_BLKS_OFF, num_data);
        hdr[0x1FE] = 0x55;
        hdr[0x1FF] = 0xAA;
    }

    write16(vmap + VMAP_SIG, BOOT_SIG);

    return (int)reserved;
}

/* Minimum disk size (KB) for which every volume can hold at least
 * min-viable blocks (so all MAX_VOLUMES volumes can be mounted at boot). */
int mkdisk_min_size_kb(uint32_t kern_size, uint32_t ccp_size, int xip)
{
    uint32_t reserved = reserve_kernel_ccp(kern_size, ccp_size, xip);

    uint32_t min_secs = (uint32_t)KERN_START_SEC + reserved +
                        (uint32_t)MAX_VOLUMES * min_viable_blocks() * BD_BLOCK_SECS;
    return (int)((min_secs + 1) / 2);
}

int mkdisk_max_size_kb(uint32_t kern_size, uint32_t ccp_size, int xip)
{
    uint32_t reserved = reserve_kernel_ccp(kern_size, ccp_size, xip);

    uint32_t cap_secs = (uint32_t)BD_VOL_MAX_BLOCKS * BD_BLOCK_SECS;
    uint32_t max_secs = (uint32_t)KERN_START_SEC + reserved + cap_secs;
    return (int)((max_secs + 1) / 2);
}
