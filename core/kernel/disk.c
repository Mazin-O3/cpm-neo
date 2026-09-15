/*
 * kernel/disk.c — Block/run volume-map disk layer
 *
 * Manages on-disk volume records (VolRec[MAX_VOLUMES]) and the block grid
 * geometry.  No free bitmap is kept: free runs are computed on demand from
 * the volume run lists.
 */

#include <abi.h>
#include <errno.h>
#include <string.h>

#include "bios.h"
#include "disk.h"
#include "disk_format.h"

#define DISK_DEFAULT_MOUNT_BLOCKS 64

typedef struct
{
    uint16_t start; /* First block index */
    uint16_t count; /* Number of blocks  */
} BlockRun;         /* 4 bytes */

typedef struct
{
    BlockRun run[VOL_MAX_RUNS]; /* Ordered; run[0] = head */
    uint8_t  run_count;         /* 0 = unmounted              */
    uint8_t  attr;              /* VOL_ATTR_RW / VOL_ATTR_RO  */
} VolRec;                       /* 18 bytes               */

typedef struct
{
    VolRec   volumes[MAX_VOLUMES];
    uint16_t num_blocks;
    uint16_t base_sec;
    uint8_t  initialized;
    uint8_t  xip; /* Cached S0_XIP flag */

    /* Single-sector write-back correctness cache. Exists to guarantee
     * read-after-write (read-your-own-writes) regardless of the platform's
     * storage behavior. */
    uint8_t  wb_buf[DISK_SECTOR_SIZE];
    uint16_t wb_sec; /* Physical sector, post-translation */
    uint8_t  wb_valid;
} DiskState;

static DiskState g_disk;

/* True if vol_id is a valid volume id and the disk layer is initialized. */
static inline int volume_valid(int8_t vol_id)
{
    return vol_id >= 0 && vol_id < MAX_VOLUMES && g_disk.initialized;
}

/* Minimum viable volume size, rounded up to whole 1K blocks. */
static uint16_t min_viable_blocks(void)
{
    return (uint16_t)((DISK_MIN_VOL_SECS + DISK_BLOCK_SECS - 1) / DISK_BLOCK_SECS);
}

static uint16_t volume_blocks(const VolRec *vr)
{
    uint16_t blocks = 0;

    for (uint8_t i = 0; i < vr->run_count; i++)
        blocks += vr->run[i].count;

    return blocks;
}

/* Gather every used block run [start, end) across all volumes into an
 * array sorted by start.  Returns the number of runs, or -1 on a layout
 * error (run count/range violation).  Runs are NOT merged here; overlap
 * is treated as an error by the caller. */
static int collect_used_runs(uint16_t *rstart, uint16_t *rend, int cap)
{
    int n = 0;

    for (int8_t v = 0; v < MAX_VOLUMES; v++)
    {
        const VolRec *vr = &g_disk.volumes[v];

        if (vr->run_count > VOL_MAX_RUNS)
            return -1;

        for (int i = 0; i < vr->run_count; i++)
        {
            if (vr->run[i].count == 0)
                return -1;

            uint32_t end = (uint32_t)vr->run[i].start + vr->run[i].count;

            if (end > g_disk.num_blocks)
                return -1;

            if (n >= cap)
                return -1;

            rstart[n] = vr->run[i].start;
            rend[n] = (uint16_t)end;
            n++;
        }
    }

    /* Insertion sort by start (n <= 16, so this is cheap). */

    for (int i = 1; i < n; i++)
    {
        uint16_t s = rstart[i];
        uint16_t e = rend[i];
        int      j = i - 1;

        while (j >= 0 && rstart[j] > s)
        {
            rstart[j + 1] = rstart[j];
            rend[j + 1] = rend[j];
            j--;
        }

        rstart[j + 1] = s;
        rend[j + 1] = e;
    }

    return n;
}

/* Validate that runs are in range, non-empty, and non-overlapping.
 * Returns 0 on success, -1 on a layout error. */
static int validate_layout(void)
{
    uint16_t s[MAX_VOLUMES * VOL_MAX_RUNS];
    uint16_t e[MAX_VOLUMES * VOL_MAX_RUNS];
    int      n = collect_used_runs(s, e, MAX_VOLUMES * VOL_MAX_RUNS);

    if (n < 0)
        return -1;

    for (int i = 1; i < n; i++)
    {
        if (s[i] < e[i - 1])
            return -1; /* Overlapping / duplicate */
    }

    return 0;
}

/* Find n contiguous free blocks; returns 0 and sets *start, or -1. */
static int find_free_run(uint16_t n, uint16_t *start)
{
    uint16_t s[MAX_VOLUMES * VOL_MAX_RUNS];
    uint16_t e[MAX_VOLUMES * VOL_MAX_RUNS];
    int      nruns = collect_used_runs(s, e, MAX_VOLUMES * VOL_MAX_RUNS);

    if (nruns < 0)
        return -1;

    uint16_t pos = 0;

    for (int i = 0; i < nruns; i++)
    {
        if (s[i] - pos >= n)
        {
            *start = pos;
            return 0;
        }

        if (e[i] > pos)
            pos = e[i];
    }

    if (g_disk.num_blocks - pos >= n)
    {
        *start = pos;
        return 0;
    }

    return -1;
}

/* Total free blocks in the grid: the gaps between the used runs. */
uint16_t disk_free_blocks(void)
{
    uint16_t s[MAX_VOLUMES * VOL_MAX_RUNS];
    uint16_t e[MAX_VOLUMES * VOL_MAX_RUNS];
    int      nruns = collect_used_runs(s, e, MAX_VOLUMES * VOL_MAX_RUNS);

    if (nruns < 0)
        return 0;

    uint32_t free_blocks = 0;
    uint16_t pos = 0;

    for (int i = 0; i < nruns; i++)
    {
        free_blocks += (uint32_t)s[i] - pos;

        if (e[i] > pos)
            pos = e[i];
    }

    free_blocks += (uint32_t)g_disk.num_blocks - pos;

    return (uint16_t)free_blocks;
}

/* Test whether the block range [start, start+n) is entirely free. */
static int range_is_free(uint16_t start, uint16_t n)
{
    uint16_t s[MAX_VOLUMES * VOL_MAX_RUNS];
    uint16_t e[MAX_VOLUMES * VOL_MAX_RUNS];
    int      nruns = collect_used_runs(s, e, MAX_VOLUMES * VOL_MAX_RUNS);

    if (nruns < 0)
        return 0;

    uint32_t lo = start;
    uint32_t hi = (uint32_t)start + n;

    for (int i = 0; i < nruns; i++)
    {
        if ((uint32_t)s[i] < hi && (uint32_t)e[i] > lo)
            return 0;
    }

    return 1;
}

/* Translate a volume-relative sector index through the volume's block runs
 * into a physical disk sector. */
int disk_translate(int8_t vol_id, uint16_t sec, uint16_t *phy_sec)
{
    if (!volume_valid(vol_id))
        return EINVAL;

    VolRec *vr = &g_disk.volumes[vol_id];

    if (vr->run_count == 0)
        return EINVAL;

    uint16_t sofar = 0;

    for (uint8_t i = 0; i < vr->run_count; i++)
    {
        uint16_t seg = vr->run[i].count * DISK_BLOCK_SECS;

        if (sec < sofar + seg)
        {
            *phy_sec = g_disk.base_sec + vr->run[i].start * DISK_BLOCK_SECS + (sec - sofar);
            return EOK;
        }

        sofar += seg;
    }

    return ENOENT;
}

/* Persist the current VolRec[4] + geometry to the VMAP sector. This is the
 * single atomic commit point for every volume operation. */
static int vmap_persist(void)
{
    uint8_t buf[DISK_SECTOR_SIZE];
    memset(buf, 0, sizeof(buf));
    write16(buf + VMAP_NUM_BLOCKS, g_disk.num_blocks);
    write16(buf + VMAP_BASE_SEC, g_disk.base_sec);
    write16(buf + VMAP_MAGIC_OFF, VMAP_MAGIC);
    memcpy(buf + VMAP_VOLREC, g_disk.volumes, sizeof(g_disk.volumes));
    write16(buf + VMAP_SIG, BOOT_SIG);

    return bios_write(VMAP_SEC, buf) ? EIO : EOK;
}

int disk_init(void)
{
    uint8_t buf[DISK_SECTOR_SIZE];

    g_disk.wb_valid = 0;

    if (bios_read(VMAP_SEC, buf) != 0)
        return EIO;

    g_disk.num_blocks = read16(buf + VMAP_NUM_BLOCKS);
    g_disk.base_sec = read16(buf + VMAP_BASE_SEC);

    if (read16(buf + VMAP_MAGIC_OFF) != VMAP_MAGIC)
        return EBADFS;

    if (g_disk.num_blocks == 0 || g_disk.num_blocks > DISK_VOL_MAX_BLOCKS)
        return EBADFS;

    if (g_disk.base_sec < VMAP_SEC + 1)
        return EBADFS;

    memcpy(g_disk.volumes, buf + VMAP_VOLREC, sizeof(g_disk.volumes));

    if (validate_layout() != 0)
        return EBADFS;

    if (bios_read(BOOT_SEC, buf) == 0)
        g_disk.xip = buf[S0_XIP];

    g_disk.initialized = 1;
    return EOK;
}

int disk_xip(void)
{
    return g_disk.initialized ? g_disk.xip : 0;
}

/* Flush the write-back cache to the platform. On write failure the cache
 * stays dirty so a later disk_sync() can retry. */
static int wb_flush(void)
{
    if (!g_disk.wb_valid)
        return EOK;

    if (bios_write(g_disk.wb_sec, g_disk.wb_buf) != 0)
        return EIO;

    g_disk.wb_valid = 0;
    return EOK;
}

int volume_read(int8_t vol_id, uint16_t sec, uint8_t *buf)
{
    uint16_t phy_sec;

    if (!buf)
        return EINVAL;

    int rc = disk_translate(vol_id, sec, &phy_sec);

    if (rc != EOK)
        return rc;

    /* Serve the cached sector so a read observes the caller's own write. */
    if (g_disk.wb_valid && g_disk.wb_sec == phy_sec)
    {
        memcpy(buf, g_disk.wb_buf, DISK_SECTOR_SIZE);
        return EOK;
    }

    return bios_read(phy_sec, buf) ? EIO : EOK;
}

int volume_write(int8_t vol_id, uint16_t sec, const uint8_t *buf)
{
    uint16_t phy_sec;

    if (!buf)
        return EINVAL;

    int rc = disk_translate(vol_id, sec, &phy_sec);

    if (rc != EOK)
        return rc;

    if (g_disk.volumes[vol_id].attr & VOL_ATTR_RO)
        return EVOLRO;

    if (g_disk.wb_valid && g_disk.wb_sec != phy_sec)
    {
        int rc = wb_flush();

        if (rc != EOK)
            return rc;
    }

    memcpy(g_disk.wb_buf, buf, DISK_SECTOR_SIZE);
    g_disk.wb_sec = phy_sec;
    g_disk.wb_valid = 1;

    return EOK;
}

/* Flush the write-back cache, then enforce durability at the platform. The
 * cache must be flushed BEFORE bios_sync() so data reaches the platform's
 * persistence layer before the barrier is requested. */
int disk_sync(void)
{
    int rc = wb_flush();

    if (rc != EOK)
        return rc;

    return bios_sync() ? EIO : EOK;
}

int volume_mount(int8_t vol_id)
{
    if (!volume_valid(vol_id))
        return EINVAL;

    VolRec *vr = &g_disk.volumes[vol_id];

    if (vr->run_count != 0)
        return EINVAL;

    uint16_t n = DISK_DEFAULT_MOUNT_BLOCKS;
    uint16_t start;

    if (find_free_run(n, &start) != 0)
    {
        n = min_viable_blocks();

        if (find_free_run(n, &start) != 0)
            return ENOSPC;
    }

    vr->run_count = 1;
    vr->run[0].start = start;
    vr->run[0].count = n;
    vr->attr = VOL_ATTR_RW;

    return vmap_persist();
}

static int volume_extend(int8_t vol_id, uint16_t n)
{
    if (!volume_valid(vol_id))
        return EINVAL;

    VolRec *vr = &g_disk.volumes[vol_id];

    if (vr->run_count == 0)
        return EINVAL;

    if (n == 0)
        return EINVAL;

    if (vr->attr & VOL_ATTR_RO)
        return EVOLRO;

    /* Prefer to extend the last run's tail when the blocks right after it
     * are free and contiguous. */
    BlockRun *last = &vr->run[vr->run_count - 1];
    uint16_t  tail = (uint16_t)(last->start + last->count);

    if (tail + n <= g_disk.num_blocks && range_is_free(tail, n))
    {
        VolRec save = *vr;
        last->count = last->count + n;

        if (vmap_persist() != EOK)
        {
            *vr = save;
            return EIO;
        }

        return EOK;
    }

    /* Otherwise gather a fresh contiguous run in a new run entry. */

    if (vr->run_count >= VOL_MAX_RUNS)
        return ENOSPC;

    uint16_t start;

    if (find_free_run(n, &start) != 0)
        return ENOSPC;

    VolRec save = *vr;
    vr->run[vr->run_count].start = start;
    vr->run[vr->run_count].count = n;
    vr->run_count++;

    if (vmap_persist() != EOK)
    {
        *vr = save;
        return EIO;
    }

    return EOK;
}

static int volume_shrink(int8_t vol_id, uint16_t n)
{
    if (!volume_valid(vol_id))
        return EINVAL;

    VolRec *vr = &g_disk.volumes[vol_id];

    if (vr->run_count == 0)
        return EINVAL;

    if (n == 0)
        return EINVAL;

    if (vr->attr & VOL_ATTR_RO)
        return EVOLRO;

    uint16_t cur = volume_blocks(vr);

    if (n >= cur)
        return EINVAL;

    if (cur - n < min_viable_blocks())
        return EINVAL;

    /* Trim n blocks from the tail, walking runs backwards. */
    VolRec   save = *vr;
    uint16_t todo = n;

    while (todo > 0 && vr->run_count > 0)
    {
        BlockRun *last = &vr->run[vr->run_count - 1];

        if (last->count <= todo)
        {
            todo = todo - last->count;
            vr->run_count--;
        }
        else
        {
            last->count = last->count - todo;
            todo = 0;
        }
    }

    if (todo != 0)
    {
        *vr = save;
        return EINVAL;
    }

    if (vmap_persist() != EOK)
    {
        *vr = save;
        return EIO;
    }

    return EOK;
}

int volume_resize(int8_t vol_id, int16_t delta)
{
    if (delta > 0)
        return volume_extend(vol_id, (uint16_t)delta);

    if (delta < 0)
        return volume_shrink(vol_id, (uint16_t)(0 - delta));

    return EOK;
}

int volume_unmount(int8_t vol_id)
{
    if (!volume_valid(vol_id))
        return EINVAL;

    VolRec *vr = &g_disk.volumes[vol_id];

    if (vr->run_count == 0)
        return EINVAL;

    VolRec save = *vr;
    vr->run_count = 0;

    if (vmap_persist() != EOK)
    {
        *vr = save;
        return EIO;
    }

    return EOK;
}

uint32_t volume_sectors(int8_t vol_id)
{
    if (!volume_valid(vol_id))
        return 0;

    return (uint32_t)volume_blocks(&g_disk.volumes[vol_id]) * DISK_BLOCK_SECS;
}

uint8_t volume_run_count(int8_t vol_id)
{
    if (!volume_valid(vol_id))
        return 0;

    return g_disk.volumes[vol_id].run_count;
}

int volume_getattr(int8_t vol_id, uint8_t *attr)
{
    if (!volume_valid(vol_id) || !attr)
        return EINVAL;

    *attr = g_disk.volumes[vol_id].attr;
    return EOK;
}

int volume_setattr(int8_t vol_id, uint8_t attr)
{
    if (!volume_valid(vol_id))
        return EINVAL;

    if (attr & ~VOL_ATTR_RO)
        return EINVAL;

    VolRec *vr = &g_disk.volumes[vol_id];
    VolRec  save = *vr;
    vr->attr = attr & VOL_ATTR_RO;

    if (vmap_persist() != EOK)
    {
        *vr = save;
        return EIO;
    }

    return EOK;
}

int volume_readonly(int8_t vol_id)
{
    if (!volume_valid(vol_id))
        return 0;

    return (g_disk.volumes[vol_id].attr & VOL_ATTR_RO) ? 1 : 0;
}

uint16_t disk_block_count(void)
{
    return g_disk.num_blocks;
}

uint16_t disk_base_sec(void)
{
    return g_disk.base_sec;
}
