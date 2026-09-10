/*
 * core/kernel/bdos.c — BDOS filesystem layer
 *
 * Extent-based filesystem on top of the logical disk layer.
 * Files are split into 8 KB extents (BD_BLOCKS_PER_EXTENT blocks); each
 * extent maps up to 8 data blocks.  Sector write caching lives in the disk
 * layer; BDOS deals only with filesystem semantics.
 *
 * All public bd_* functions accept a vol_id that must already be mounted
 * (bd_bind or bd_mount) — Callers never touch raw sectors.  The CHECK_VOL
 * and CHECK_FCB macros enforce this invariant at the top of each entry
 * point, returning ENOVOL/EBADF on violations.
 *
 * The block allocation bitmap is rebuilt from the directory on mount
 * (bd_rescan_alloc_map); subsequent alloc/free calls keep it in sync.
 * alloc_next provides an O(n) amortised scan by remembering where the
 * last free block was found.
 */

#include "bdos.h"
#include "ctype.h"
#include "disk.h"
#include "disk_format.h"
#include "string.h"

/* Vol-checked guard: resolves vol_id and returns ENOVOL if unmounted. */
#define CHECK_VOL(v, vol_id)                                                                       \
    Volume *v = vol_checked(vol_id);                                                               \
    if (!v)                                                                                        \
        return ENOVOL;

/* FCB-checked guard: validates fd, then applies CHECK_VOL on its volume. */
#define CHECK_FCB(f, v, fd)                                                                        \
    FCB *f = fcb_get(fd);                                                                          \
    if (!f)                                                                                        \
        return EBADF;                                                                              \
    CHECK_VOL(v, f->ctx.vol_id);

typedef struct
{
    uint16_t root_start_sec;
    uint16_t data_start_sec;
    uint16_t total_sectors;
    uint16_t total_blocks; /* Capped at BD_VOL_MAX_BLOCKS */
    uint16_t alloc_next;   /* Hint for next free-block scan */
    int8_t   id;
    uint8_t  mounted : 1;   /* Set by bd_bind/bd_mount, cleared by bd_unbind */
    uint8_t  read_only : 1; /* Mirrors VOL_ATTR_RO on the disk */
    uint8_t  block_alloc_map[BD_BLOCK_MAP_BYTES]; /* Rebuilt on mount */
} Volume;

/* FileKey: unique identity for a directory entry — Volume + 8.3 name + user. */
typedef struct
{
    Volume *v;
    char    name83[NAME83_LEN];
    uint8_t user;
} FileKey;

/* Decoded directory entry for one extent of a file. */
typedef struct
{
    uint16_t diridx;
    uint16_t blocks[BD_BLOCKS_PER_EXTENT];
    uint16_t extent_bytes;
    uint8_t  extent_idx;
    uint8_t  attrib;
} DirInfo;

/* Runtime state for an open file descriptor. */
typedef struct
{
    uint32_t  size; /* Total file size in bytes (tracked on write) */
    uint32_t  position;
    uint8_t   in_use : 1;
    uint8_t   writable : 1;
    uint8_t   name83[NAME83_LEN];
    FsContext ctx;
    DirInfo   cur;
} FCB;

typedef struct
{
    Volume  vol[MAX_VOLUMES];
    FCB     fcb[BD_MAX_FCBS];
    uint8_t sec_buf[DISK_SECTOR_SIZE];
} BDState;

typedef struct
{
    FileKey  key;
    uint8_t  extent_idx;
    DirInfo *out;
} FindExtentCtx;

typedef struct
{
    FileKey   key;
    uint16_t *first_diridx; /* Set to the first extent's dir index */
    uint16_t  n;
    uint32_t  size;  /* Logical size: sum of exact extent byte counts */
    uint32_t  alloc; /* Allocated bytes: sum of block-rounded extent sizes */
} ScanExtentsCtx;

typedef int (*dir_scan_fn)(Volume *v, const uint8_t *entry, uint16_t idx, void *ctx);

static BDState g_bd;

/* Per-volume block cap. */
static inline uint16_t bd_block_cap(void)
{
    return BD_VOL_MAX_BLOCKS;
}

static int dir_scan(Volume *v, uint8_t user, dir_scan_fn fn, void *ctx);

static int name83_match(const uint8_t *dn, const char *n83)
{
    return strncasecmp((const char *)dn, n83, NAME83_LEN) == 0;
}

static int wildmatch(const char *pat, const char *str)
{
    const char *star = 0;
    const char *mark = 0;

    for (;;)
    {
        if (*pat == '*')
        {
            star = ++pat;
            mark = str;
            continue;
        }

        if (*pat == '\0')
        {
            if (star)
                return 1;
            return *str == '\0';
        }

        if (*str != '\0' &&
            (*pat == '?' || toupper((unsigned char)*pat) == toupper((unsigned char)*str)))
        {
            pat++;
            str++;
            continue;
        }

        if (!star || *str == '\0')
            return 0;

        pat = star;
        str = ++mark;
    }
}

static inline FileKey make_key(Volume *v, const char *n83, uint8_t user)
{
    FileKey k = {.v = v, .user = user};
    memcpy(k.name83, n83, NAME83_LEN);
    return k;
}

static void entry_to_name(const uint8_t *entry, char *out)
{
    int base_len = NAME83_BASE, ext_len = NAME83_EXT, out_pos = 0;

    while (base_len > 0 && entry[base_len - 1] == ' ')
        base_len--;

    while (ext_len > 0 && entry[NAME83_BASE + ext_len - 1] == ' ')
        ext_len--;

    for (int i = 0; i < base_len; i++)
        out[out_pos++] = entry[i];

    if (ext_len > 0)
    {
        out[out_pos++] = '.';

        for (int i = 0; i < ext_len; i++)
            out[out_pos++] = entry[NAME83_BASE + i];
    }

    out[out_pos] = '\0';
}

/*
 * Copy a directory entry into its raw blank-padded base and extension
 * fields.  Matching follows real CP/M: the FDOS compares all 11 bytes,
 * where '?' matches any character (including blanks) and blanks match
 * only blanks.  No trimming — Padding participates in the comparison.
 */
static void entry_fields(const uint8_t *entry, char *base, char *ext)
{
    memcpy(base, entry, NAME83_BASE);
    base[NAME83_BASE] = '\0';
    memcpy(ext, entry + NAME83_BASE, NAME83_EXT);
    ext[NAME83_EXT] = '\0';
}

static Volume *vol_for(int8_t vol_id)
{
    return (vol_id < 0 || vol_id >= MAX_VOLUMES) ? NULL : &g_bd.vol[vol_id];
}

static Volume *vol_checked(int8_t vol_id)
{
    Volume *v = vol_for(vol_id);

    if (!v || !v->mounted)
        return NULL;

    return v;
}

static int vol_read(Volume *v, uint16_t sec, uint8_t *buf)
{
    return volume_read(v->id, sec, buf) ? EIO : EOK;
}

static int vol_write(Volume *v, uint16_t sec, const uint8_t *buf)
{
    if (v->read_only)
        return EVOLRO;

    return volume_write(v->id, sec, buf) ? EIO : EOK;
}

static int bd_write_header(Volume *v)
{
    uint8_t hdr[DISK_SECTOR_SIZE];

    if (vol_read(v, 0, hdr) != EOK)
        return EIO;

    write16(&hdr[VHDR_SIZE_KB_OFF], (uint16_t)(v->total_sectors / BD_SECTORS_PER_KB));
    write16(&hdr[VHDR_TOT_BLKS_OFF], v->total_blocks);

    return vol_write(v, 0, hdr);
}

static inline int block_is_allocated(const Volume *v, uint16_t block_num)
{
    return (v->block_alloc_map[block_num / BD_BITS_PER_BYTE] &
            (uint8_t)(1u << (block_num % BD_BITS_PER_BYTE))) != 0;
}

static inline void block_mark_allocated(Volume *v, uint16_t block_num)
{
    v->block_alloc_map[block_num / BD_BITS_PER_BYTE] |=
        (uint8_t)(1u << (block_num % BD_BITS_PER_BYTE));
}

static inline void block_mark_free(Volume *v, uint16_t block_num)
{
    v->block_alloc_map[block_num / BD_BITS_PER_BYTE] &=
        (uint8_t)~(1u << (block_num % BD_BITS_PER_BYTE));
}

static int find_free_block(Volume *v, uint16_t start)
{
    for (uint16_t i = start; i < v->total_blocks;)
    {
        if (v->block_alloc_map[i / BD_BITS_PER_BYTE] == BD_BITMAP_FULL)
        {
            i = (uint16_t)(((i / BD_BITS_PER_BYTE) + 1) * BD_BITS_PER_BYTE);
            continue;
        }

        if (!block_is_allocated(v, i))
            return (int)i;

        i++;
    }

    return -1;
}

static int alloc_block(Volume *v)
{
    int block_num = find_free_block(v, v->alloc_next);

    if (block_num < 0)
    {
        block_num = find_free_block(v, 0);

        if (block_num < 0 || (uint16_t)block_num >= v->alloc_next)
            return -1;
    }

    block_mark_allocated(v, (uint16_t)block_num);
    v->alloc_next = (uint16_t)block_num + 1;
    return block_num;
}

static void free_block(Volume *v, uint16_t block_num)
{
    if (block_num < v->total_blocks)
        block_mark_free(v, block_num);
}

static uint16_t count_free(Volume *v)
{
    uint16_t c = 0;

    for (uint16_t i = 0; i < v->total_blocks; i++)
    {
        if (!block_is_allocated(v, i))
            c++;
    }

    return c;
}

static uint16_t block_sec(Volume *v, uint16_t block_num)
{
    return v->data_start_sec + block_num * BD_BLOCK_SECS;
}

static uint16_t block_offset_sec(Volume *v, uint16_t block_num, uint32_t pos)
{
    uint32_t within = pos % BD_BLOCK_BYTES;
    return (uint16_t)(block_sec(v, block_num) + within / DISK_SECTOR_SIZE);
}

/* Load the directory sector containing |idx| into sec_buf and return a
 * pointer to the entry at |idx|, or NULL on read failure. */
static uint8_t *load_dir_entry(Volume *v, uint16_t idx)
{
    uint16_t s = idx / BD_ENTRIES_PER_SEC;

    if (vol_read(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
        return 0;

    return g_bd.sec_buf + (idx % BD_ENTRIES_PER_SEC) * BD_ENTRY_SIZE;
}

static void entry_block_list(const uint8_t *entry, uint16_t *out)
{
    for (int b = 0; b < BD_BLOCKS_PER_EXTENT; b++)
        out[b] = read16(&entry[BD_DIR_BLOCKS + b * 2]);
}

static void set_entry_block_list(uint8_t *entry, const uint16_t *blocks)
{
    for (int b = 0; b < BD_BLOCKS_PER_EXTENT; b++)
        write16(&entry[BD_DIR_BLOCKS + b * 2], blocks[b]);
}

static int rescan_alloc_cb(Volume *v, const uint8_t *entry, uint16_t idx, void *arg)
{
    (void)idx;
    (void)arg;

    uint16_t blocks[BD_BLOCKS_PER_EXTENT];
    entry_block_list(entry, blocks);

    for (int b = 0; b < BD_BLOCKS_PER_EXTENT; b++)
    {
        if (!blocks[b])
            continue;

        /*
         * A reference outside the volume or shared between two extents
         * means the directory is corrupt; mounting it would let the
         * allocator hand those blocks to new files and destroy data.
         */

        if (blocks[b] >= v->total_blocks)
            return EBADFS;

        if (block_is_allocated(v, blocks[b]))
            return EBADFS;

        block_mark_allocated(v, blocks[b]);
    }

    return EOK;
}

static int bd_rescan_alloc_map(Volume *v)
{
    memset(v->block_alloc_map, 0, sizeof(v->block_alloc_map));
    block_mark_allocated(v, BD_RESERVED_BLOCK);

    int rc = dir_scan(v, BD_USER_INVALID, rescan_alloc_cb, 0);
    return (rc == ENOENT) ? EOK : rc;
}

static int fcb_alloc(void)
{
    for (int i = 0; i < BD_MAX_FCBS; i++)
    {
        if (!g_bd.fcb[i].in_use)
            return i;
    }

    return -1;
}

static FCB *fcb_get(int fd)
{
    return (fd < 0 || fd >= BD_MAX_FCBS || !g_bd.fcb[fd].in_use) ? 0 : &g_bd.fcb[fd];
}

static void fcb_init(FCB *f, FsContext ctx, uint32_t sz, uint8_t w, uint16_t diridx)
{
    f->in_use = 1;
    f->writable = w;
    f->ctx = ctx;
    f->size = sz;
    f->position = 0;
    f->cur.diridx = diridx;
    f->cur.extent_idx = 0;
    f->cur.extent_bytes = 0;
}

static int bd_vol_has_writable_fcb(int8_t vol_id)
{
    for (int i = 0; i < BD_MAX_FCBS; i++)
    {
        if (g_bd.fcb[i].in_use && g_bd.fcb[i].writable && g_bd.fcb[i].ctx.vol_id == vol_id)
            return 1;
    }

    return 0;
}

/*
 * Pass BD_USER_INVALID to skip user-area filtering.
 * Stops early if fn returns non-zero (propagates that value).
 */
static int dir_scan(Volume *v, uint8_t user, dir_scan_fn fn, void *ctx)
{
    for (uint16_t s = 0; s < BD_ROOT_SECS; s++)
    {
        if (vol_read(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
            return EIO;

        for (uint16_t e = 0; e < BD_ENTRIES_PER_SEC; e++)
        {
            uint16_t i = s * BD_ENTRIES_PER_SEC + e;

            if (i >= BD_ROOT_ENTRIES)
                return ENOENT;

            uint8_t *entry = g_bd.sec_buf + e * BD_ENTRY_SIZE;

            if (entry[0] == BD_ENTRY_EMPTY)
                return ENOENT;

            if (entry[0] == BD_ENTRY_DELETED)
                continue;

            if (user != BD_USER_INVALID && entry[BD_DIR_USER] != user)
                continue;

            int rc = fn(v, entry, i, ctx);

            if (rc != EOK)
                return rc;
        }
    }

    return ENOENT;
}

static int find_extent_cb(Volume *v, const uint8_t *entry, uint16_t idx, void *arg)
{
    (void)v;
    (void)idx;

    FindExtentCtx *ctx = arg;

    if (entry[BD_DIR_EXTENT_IDX] != ctx->extent_idx || !name83_match(entry, ctx->key.name83))
        return EOK;

    ctx->out->diridx = idx;
    entry_block_list(entry, ctx->out->blocks);
    ctx->out->extent_bytes = read16(&entry[BD_DIR_EXTENT_BYTES]);
    ctx->out->attrib = entry[BD_DIR_ATTR];

    return DIR_SCAN_STOP;
}

static int find_extent(FileKey key, uint8_t extent_idx, DirInfo *out)
{
    Volume *v = key.v;
    uint8_t user = key.user;

    FindExtentCtx ctx = {key, extent_idx, out};

    int rc = dir_scan(v, user, find_extent_cb, &ctx);

    return (rc == DIR_SCAN_STOP) ? EOK : rc;
}

static int update_extent(Volume *v, const DirInfo *de)
{
    uint8_t *entry = load_dir_entry(v, de->diridx);

    if (!entry)
        return EIO;

    set_entry_block_list(entry, de->blocks);
    write16(&entry[BD_DIR_EXTENT_BYTES], de->extent_bytes);
    entry[BD_DIR_EXTENT_IDX] = de->extent_idx;
    entry[BD_DIR_ATTR] = de->attrib;

    return vol_write(v, v->root_start_sec + de->diridx / BD_ENTRIES_PER_SEC, g_bd.sec_buf);
}

static void fill_dir_entry(uint8_t *entry, const FileKey *key, const DirInfo *de,
                           uint16_t first_block)
{
    memset(entry, 0, BD_ENTRY_SIZE);
    memcpy(entry, key->name83, NAME83_LEN);
    entry[BD_DIR_ATTR] = de->attrib;
    entry[BD_DIR_USER] = key->user;
    entry[BD_DIR_EXTENT_IDX] = de->extent_idx;
    memset(&entry[BD_DIR_BLOCKS], 0, BD_BLOCKS_PER_EXTENT * 2);
    write16(&entry[BD_DIR_BLOCKS], first_block);
}

static int create_extent(FileKey key, const DirInfo *de, uint16_t first_block, uint16_t *out_diridx)
{
    Volume *v = key.v;

    for (uint16_t s = 0; s < BD_ROOT_SECS; s++)
    {
        if (vol_read(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
            return EIO;

        for (uint16_t e = 0; e < BD_ENTRIES_PER_SEC; e++)
        {
            uint16_t i = s * BD_ENTRIES_PER_SEC + e;

            if (i >= BD_ROOT_ENTRIES)
                break;

            uint8_t *entry = g_bd.sec_buf + e * BD_ENTRY_SIZE;

            if (entry[0] == BD_ENTRY_EMPTY || entry[0] == BD_ENTRY_DELETED)
            {
                fill_dir_entry(entry, &key, de, first_block);

                if (out_diridx)
                    *out_diridx = i;

                return vol_write(v, v->root_start_sec + s, g_bd.sec_buf);
            }
        }
    }

    return EDIRFULL;
}

static int scan_extents_cb(Volume *vol, const uint8_t *entry, uint16_t diridx, void *arg)
{
    (void)vol;

    ScanExtentsCtx *ctx = arg;

    if (!name83_match(entry, ctx->key.name83))
        return EOK;

    if (ctx->n >= BD_MAX_EXTENTS)
        return EBADFS;

    uint16_t extent_bytes = read16(&entry[BD_DIR_EXTENT_BYTES]);

    if (ctx->n == 0 && ctx->first_diridx)
        *ctx->first_diridx = diridx;

    ctx->size += extent_bytes;
    ctx->alloc += (uint32_t)BD_BLOCK_BYTES * ((extent_bytes + BD_BLOCK_BYTES - 1) / BD_BLOCK_BYTES);
    ctx->n++;

    return EOK;
}

static int scan_extents(FileKey key, uint16_t *first_diridx, int *count, uint32_t *out_size,
                        uint32_t *out_alloc)
{
    ScanExtentsCtx ctx = {
        .key = key,
        .first_diridx = first_diridx,
    };

    int rc = dir_scan(key.v, key.user, scan_extents_cb, &ctx);

    *count = (int)ctx.n;

    if (out_size)
        *out_size = ctx.size;

    if (out_alloc)
        *out_alloc = ctx.alloc;

    return (rc == ENOENT) ? EOK : rc;
}

static int resolve_extent(FCB *f, Volume *v)
{
    uint32_t block_idx = f->position / BD_BLOCK_BYTES;
    uint32_t extent_idx = block_idx / BD_BLOCKS_PER_EXTENT;
    uint32_t block_off = block_idx % BD_BLOCKS_PER_EXTENT;

    if (extent_idx != f->cur.extent_idx)
    {
        if (extent_idx > UINT8_MAX)
            return -1;

        DirInfo di;
        int     rc = find_extent(make_key(v, (const char *)f->name83, f->ctx.user_area),
                                 (uint8_t)extent_idx, &di);

        if (rc != EOK)
            return -1;

        f->cur = di;
    }

    return (int)block_off;
}

static int fcb_flush(FCB *f, Volume *v)
{
    return update_extent(v, &f->cur);
}

/*
 * Bind an existing formatted volume.  Closes stale FCBs from any
 * previous bind on the same vol_id, then rescans the alloc map.
 */
int bd_bind(int8_t vol_id)
{
    Volume *v = vol_for(vol_id);

    if (!v)
        return ENOVOL;

    for (int i = 0; i < BD_MAX_FCBS; i++)
    {
        if (g_bd.fcb[i].ctx.vol_id == vol_id)
            g_bd.fcb[i].in_use = 0;
    }

    uint8_t hdr[DISK_SECTOR_SIZE];
    v->id = vol_id;
    v->mounted = 0;

    uint8_t attr = VOL_ATTR_RW;

    int rc = volume_getattr(vol_id, &attr);

    if (rc != EOK)
        return rc;

    v->read_only = attr & VOL_ATTR_RO;

    if (volume_read(vol_id, 0, hdr) != EOK)
        return EIO;

    if (read16(&hdr[VHDR_MAGIC_OFF]) != DISK_MAGIC)
        return EBADFS;

    v->root_start_sec = read16(&hdr[VHDR_ROOT_SEC_OFF]);
    v->data_start_sec = read16(&hdr[VHDR_DATA_SEC_OFF]);
    v->total_sectors = read16(&hdr[VHDR_SIZE_KB_OFF]) * BD_SECTORS_PER_KB;
    v->total_blocks = read16(&hdr[VHDR_TOT_BLKS_OFF]);

    if (!v->total_blocks || v->total_blocks > bd_block_cap())
        return EBADFS;

    if ((uint32_t)v->total_blocks * BD_BLOCK_SECS > BD_DISK_MAX_SECS)
        return EBADFS;

    if (v->total_sectors > (uint16_t)volume_sectors(vol_id))
        return EBADFS;

    rc = bd_rescan_alloc_map(v);

    if (rc != EOK)
        return rc;

    uint16_t stride = v->total_blocks / MAX_VOLUMES;
    v->alloc_next = stride ? ((uint16_t)vol_id * stride) % v->total_blocks : 1;

    if (v->alloc_next == BD_RESERVED_BLOCK)
        v->alloc_next = 1;

    v->mounted = 1;

    return EOK;
}

/*
 * Format and bind a fresh volume (SET MT).  Requires a prior
 * volume_mount() call.
 */
int bd_mount(int8_t vol_id)
{
    Volume *v = vol_for(vol_id);

    if (!v)
        return ENOVOL;

    if (v->mounted)
        return EINVAL;

    int rc = volume_mount(vol_id);

    if (rc != EOK)
        return rc;

    uint16_t n_secs = (uint16_t)volume_sectors(vol_id);
    uint16_t data_start = (uint16_t)(BD_HEADER_SECS + BD_ROOT_SECS);
    uint16_t num_data = (uint16_t)((n_secs - data_start) / BD_BLOCK_SECS);

    if (num_data == 0 || num_data > bd_block_cap())
    {
        volume_unmount(vol_id);
        return EBADFS;
    }

    memset(g_bd.sec_buf, 0, DISK_SECTOR_SIZE);
    write16(g_bd.sec_buf + VHDR_MAGIC_OFF, DISK_MAGIC);
    write16(g_bd.sec_buf + VHDR_VER_OFF, VHDR_VER);
    write16(g_bd.sec_buf + VHDR_SIZE_KB_OFF, (uint16_t)(n_secs / BD_SECTORS_PER_KB));
    write16(g_bd.sec_buf + VHDR_ROOT_SEC_OFF, BD_HEADER_SECS);
    write16(g_bd.sec_buf + VHDR_DATA_SEC_OFF, data_start);
    write16(g_bd.sec_buf + VHDR_TOT_BLKS_OFF, num_data);

    if (volume_write(vol_id, 0, g_bd.sec_buf) != EOK)
    {
        volume_unmount(vol_id);
        return EIO;
    }

    for (uint16_t s = 0; s < BD_ROOT_SECS; s++)
    {
        memset(g_bd.sec_buf, 0, DISK_SECTOR_SIZE);

        if (volume_write(vol_id, (uint16_t)(BD_HEADER_SECS + s), g_bd.sec_buf) != EOK)
        {
            volume_unmount(vol_id);
            return EIO;
        }
    }

    return bd_bind(vol_id);
}

/*
 * Resize a mounted volume.  Positive delta grows, negative shrinks (by
 * |delta|), zero is a no-op.  Grow fails with EVOLRO on read-only
 * volumes; shrink returns EPERM if any target blocks are allocated,
 * EINVAL if the result would fall below BD_MIN_VOL_SECS.
 */
int bd_resize(int8_t vol_id, int16_t delta)
{
    CHECK_VOL(v, vol_id);

    if (delta == 0)
        return EOK;

    if (v->read_only)
        return EVOLRO;

    if (delta > 0)
    {
        uint16_t n = (uint16_t)delta;
        uint16_t old_secs = v->total_sectors;
        uint16_t old_blocks = v->total_blocks;

        if (old_blocks >= bd_block_cap())
            return ENOSPC;

        uint16_t max_extra = bd_block_cap() - old_blocks;

        if (n > max_extra)
            n = max_extra;

        int rc = volume_resize(vol_id, (int16_t)n);

        if (rc != EOK)
            return rc;

        v->total_sectors = (uint16_t)volume_sectors(vol_id);
        v->total_blocks = (uint16_t)((v->total_sectors - v->data_start_sec) / BD_BLOCK_SECS);

        if (v->total_blocks > bd_block_cap())
            v->total_blocks = bd_block_cap();

        rc = bd_write_header(v);

        if (rc != EOK)
        {
            if (volume_resize(vol_id, (int16_t)(0 - n)) == EOK)
            {
                v->total_sectors = old_secs;
                v->total_blocks = old_blocks;
                bd_write_header(v);
            }

            return EIO;
        }

        return EOK;
    }

    uint16_t n = (uint16_t)(0 - delta);

    if (n >= v->total_blocks)
        return EINVAL;

    uint32_t removed = n;
    uint32_t new_total_secs = (uint32_t)v->total_sectors - removed * BD_BLOCK_SECS;

    if (new_total_secs < BD_MIN_VOL_SECS)
        return EINVAL;

    uint32_t new_total_blocks = v->total_blocks - removed;

    for (uint32_t b = new_total_blocks; b < v->total_blocks; b++)
    {
        if (block_is_allocated(v, (uint16_t)b))
            return EPERM;
    }

    uint16_t old_secs = v->total_sectors;
    uint16_t old_blocks = v->total_blocks;

    int rc = volume_resize(vol_id, delta);

    if (rc != EOK)
        return rc;

    v->total_sectors = (uint16_t)volume_sectors(vol_id);
    v->total_blocks = (uint16_t)new_total_blocks;

    rc = bd_write_header(v);

    if (rc != EOK)
    {
        /* Undo the shrink so VMAP, header and RAM geometry agree. */

        if (volume_resize(vol_id, (int16_t)(0 - delta)) == EOK)
        {
            v->total_sectors = old_secs;
            v->total_blocks = old_blocks;
            bd_write_header(v);
        }

        return EIO;
    }

    return EOK;
}

/*
 * Unbind a volume.  Returns EPERM if the volume still has allocated
 * data blocks (not empty).
 */
int bd_unbind(int8_t vol_id)
{
    Volume *v = vol_for(vol_id);

    if (!v)
        return ENOVOL;

    if (!v->mounted)
        return EINVAL;

    for (int i = 0; i < BD_MAX_FCBS; i++)
    {
        if (g_bd.fcb[i].ctx.vol_id == vol_id)
            g_bd.fcb[i].in_use = 0;
    }

    int rrc = bd_rescan_alloc_map(v);

    if (rrc != EOK)
        return rrc;

    uint16_t map_bytes = (uint16_t)((v->total_blocks + BD_BITS_PER_BYTE - 1) / BD_BITS_PER_BYTE);

    uint8_t reserved_bit = (uint8_t)(1u << (BD_RESERVED_BLOCK % BD_BITS_PER_BYTE));

    if (v->block_alloc_map[BD_RESERVED_BLOCK / BD_BITS_PER_BYTE] & (uint8_t)~reserved_bit)
        return EPERM;

    for (uint16_t i = 1; i < map_bytes; i++)
    {
        if (v->block_alloc_map[i])
            return EPERM;
    }

    int rc = volume_unmount(vol_id);

    if (rc != EOK)
        return rc;

    v->mounted = 0;

    return EOK;
}

/*
 * Complete pending filesystem synchronization and enforce physical
 * persistence, then refresh free-block hints for idle volumes (those with
 * no open writable files).
 */
int bd_sync(void)
{
    int rc = disk_sync();

    if (rc != EOK)
        return rc;

    for (int8_t v = 0; v < MAX_VOLUMES; v++)
    {
        Volume *vol = &g_bd.vol[v];

        if (!vol->mounted || bd_vol_has_writable_fcb(v))
            continue;

        rc = bd_rescan_alloc_map(vol);

        if (rc != EOK)
            return rc;

        int block_num = find_free_block(vol, 1);
        vol->alloc_next = (block_num >= 0) ? (uint16_t)block_num : vol->total_blocks;
    }

    return EOK;
}

int bd_vstat(int8_t vol_id, VolStat *stat)
{
    CHECK_VOL(v, vol_id);

    uint16_t usable = v->total_blocks > 0 ? (uint16_t)(v->total_blocks - 1) : 0;

    stat->total_blocks = usable;
    stat->free_blocks = count_free(v);
    stat->read_only = v->read_only;

    return EOK;
}

int bd_vsetattr(int8_t vol_id, uint8_t attr)
{
    CHECK_VOL(v, vol_id);

    if (attr & (uint8_t)~VOL_ATTR_RO)
        return EINVAL;

    int rc = volume_setattr(vol_id, attr & VOL_ATTR_RO);

    if (rc != EOK)
        return rc;

    v->read_only = attr & VOL_ATTR_RO;

    return EOK;
}

/*
 * Open an existing file.  Returns a non-negative fd on success,
 * or EFILERO/EPERM/ENOVOL/ENFILE on error.
 */
int bd_open(const char *name83, FsContext ctx, uint8_t writable)
{
    CHECK_VOL(v, ctx.vol_id);

    if (writable && v->read_only)
        return EVOLRO;

    int fd = fcb_alloc();

    if (fd < 0)
        return ENFILE;

    FCB *f = &g_bd.fcb[fd];

    DirInfo di;
    int     rc = find_extent(make_key(v, name83, ctx.user_area), 0, &di);

    if (rc != EOK)
    {
        f->in_use = 0;
        return rc;
    }

    if (writable && (di.attrib & FILE_ATTR_READ_ONLY))
    {
        f->in_use = 0;
        return EFILERO;
    }

    uint8_t *rep = load_dir_entry(v, di.diridx);

    if (rep)
        memcpy(f->name83, rep, NAME83_LEN);
    else
        memcpy(f->name83, name83, NAME83_LEN);

    uint32_t total;
    int      num_extents;

    rc = scan_extents(make_key(v, name83, ctx.user_area), 0, &num_extents, &total, 0);

    if (rc != EOK)
    {
        f->in_use = 0;
        return rc;
    }

    fcb_init(f, ctx, total, writable, di.diridx);
    f->cur = di;

    return fd;
}

/*
 * Read up to len bytes at the current position.  May return fewer
 * bytes than requested at EOF or on extent boundary.
 */
int bd_read(int fd, uint8_t *buf, uint16_t len)
{
    CHECK_FCB(f, v, fd);

    uint32_t rem = f->size - f->position;
    uint32_t br = 0;

    if (len > rem)
        len = (uint16_t)rem;

    while (br < len)
    {
        int block_off = resolve_extent(f, v);

        if (block_off < 0 || block_off >= BD_BLOCKS_PER_EXTENT)
            break;

        uint16_t sec = block_offset_sec(v, f->cur.blocks[block_off], f->position);

        uint16_t off = f->position % DISK_SECTOR_SIZE;
        uint32_t sl = DISK_SECTOR_SIZE - off;
        uint32_t remain = len - br;

        if (vol_read(v, sec, g_bd.sec_buf) != EOK)
            return br ? (int)br : EIO;

        uint32_t tc = (remain > sl) ? sl : remain;

        memcpy(buf + br, &g_bd.sec_buf[off], tc);

        br += tc;
        f->position += tc;
    }

    return (int)br;
}

/*
 * Write up to len bytes.  Returns bytes written (may be short at
 * ENOSPC), or a negative error code.
 */
int bd_write(int fd, const uint8_t *buf, uint16_t len)
{
    CHECK_FCB(f, v, fd);

    if (!f->writable)
        return EFILERO;

    uint16_t bw = 0;

    while (bw < len)
    {
        uint32_t block_idx = f->position / BD_BLOCK_BYTES;
        uint32_t extent_idx = block_idx / BD_BLOCKS_PER_EXTENT;

        if (extent_idx != f->cur.extent_idx)
        {
            if (extent_idx >= BD_MAX_EXTENTS)
                return bw ? (int)bw : ENOSPC;

            int flrc = fcb_flush(f, v);

            if (flrc != EOK)
                return bw ? (int)bw : flrc;

            DirInfo fdi;

            int find_rc = find_extent(make_key(v, (const char *)f->name83, f->ctx.user_area),
                                      (uint8_t)extent_idx, &fdi);

            if (find_rc == EOK)
            {
                f->cur = fdi;
            }
            else if (find_rc == ENOENT)
            {
                int new_block = alloc_block(v);

                if (new_block < 0)
                    return bw ? (int)bw : ENOSPC;

                DirInfo nde = {.extent_idx = (uint8_t)extent_idx, .attrib = f->cur.attrib};

                uint16_t ndi;

                int rc = create_extent(make_key(v, (const char *)f->name83, f->ctx.user_area), &nde,
                                       (uint16_t)new_block, &ndi);

                if (rc != EOK)
                {
                    free_block(v, (uint16_t)new_block);
                    return bw ? (int)bw : rc;
                }

                f->cur = nde;
                f->cur.diridx = ndi;
                f->cur.blocks[0] = (uint16_t)new_block;
            }
            else
            {
                return bw ? (int)bw : find_rc;
            }
        }

        int block_off = (int)(block_idx % BD_BLOCKS_PER_EXTENT);

        int new_block = 0;

        if (f->cur.blocks[block_off] == 0)
        {
            int block_num = alloc_block(v);

            if (block_num < 0)
                return bw ? (int)bw : ENOSPC;

            f->cur.blocks[block_off] = (uint16_t)block_num;
            memset(g_bd.sec_buf, 0, DISK_SECTOR_SIZE);
            new_block = 1;
        }

        uint16_t sec = block_offset_sec(v, f->cur.blocks[block_off], f->position);

        uint16_t off = f->position % DISK_SECTOR_SIZE;
        uint32_t sl = DISK_SECTOR_SIZE - off;
        uint32_t remain = (uint32_t)(len - bw);
        uint32_t tc = (remain > sl) ? sl : remain;

        if (!new_block && (off > 0 || tc < sl))
        {
            if (vol_read(v, sec, g_bd.sec_buf) != EOK)
                return bw ? (int)bw : EIO;
        }

        memcpy(&g_bd.sec_buf[off], buf + bw, tc);

        if (vol_write(v, sec, g_bd.sec_buf) != EOK)
            return bw ? (int)bw : EIO;

        bw += (uint16_t)tc;
        f->position += tc;

        uint32_t extent_max = BD_EXTENT_BYTES;
        uint32_t new_extent_size = f->position % extent_max;

        if (new_extent_size == 0 && f->position >= extent_max)
            new_extent_size = extent_max;

        if (new_extent_size > f->cur.extent_bytes)
            f->cur.extent_bytes = (uint16_t)new_extent_size;

        if (f->position > f->size)
            f->size = f->position;
    }

    return (int)bw;
}

/*
 * Close a file descriptor, finalizing pending filesystem state.  Directory
 * entries are written through vol_write to the disk-layer cache; durability
 * is enforced centrally by bd_sync(), not at close time.
 */
int bd_close(int fd)
{
    FCB *f = fcb_get(fd);

    if (!f)
        return EBADF;

    Volume *v = vol_for(f->ctx.vol_id);
    int     rc = EOK;

    if (f->writable)
    {
        if (!v || !v->mounted)
            rc = ENOVOL;
        else
            rc = fcb_flush(f, v);
    }

    memset(f, 0, sizeof(FCB));

    return rc;
}

int bd_seek(int fd, uint32_t offset)
{
    CHECK_FCB(f, v, fd);

    if (offset > f->size)
        offset = f->size;

    f->position = offset;

    return EOK;
}

/*
 * Search the directory for files matching a wildcard pattern.
 * Returns a 1-based directory index on match, or ENOENT.
 * Pass start_pos to resume a previous scan.
 */
int bd_find(const char *pat, FsContext ctx, FileInfo *out, uint16_t start_pos)
{
    CHECK_VOL(v, ctx.vol_id);

    /* Callers must supply the padded 8.3 form (see make_name83):
     * base at pat[0..7], extension at pat[8..10].  Fields are compared
     * raw — '?' matches any byte, blanks only blanks — So an ambiguous
     * reference never crosses the '.' boundary. */
    char base_pat[NAME83_BASE + 1], ext_pat[NAME83_EXT + 1];

    memcpy(base_pat, pat, NAME83_BASE);
    base_pat[NAME83_BASE] = '\0';

    memcpy(ext_pat, pat + NAME83_BASE, NAME83_EXT);
    ext_pat[NAME83_EXT] = '\0';

    uint16_t start_s = start_pos / BD_ENTRIES_PER_SEC;

    for (uint16_t s = start_s; s < BD_ROOT_SECS; s++)
    {
        if (vol_read(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
            return EIO;

        uint16_t first_e = (s == start_s) ? (start_pos % BD_ENTRIES_PER_SEC) : 0;

        for (uint16_t e = first_e; e < BD_ENTRIES_PER_SEC; e++)
        {
            uint16_t i = s * BD_ENTRIES_PER_SEC + e;

            if (i >= BD_ROOT_ENTRIES)
                return ENOENT;

            uint8_t *entry = g_bd.sec_buf + e * BD_ENTRY_SIZE;

            if (entry[0] == BD_ENTRY_EMPTY)
                return ENOENT;

            if (entry[0] == BD_ENTRY_DELETED || entry[BD_DIR_USER] != ctx.user_area)
                continue;

            if (entry[BD_DIR_EXTENT_IDX] != 0)
                continue;

            char name[FILENAME_MAX];

            entry_to_name(entry, name);

            char cand_base[NAME83_BASE + 1];
            char cand_ext[NAME83_EXT + 1];

            entry_fields(entry, cand_base, cand_ext);

            if (!wildmatch(base_pat, cand_base) || !wildmatch(ext_pat, cand_ext))
                continue;

            uint8_t match_buf[BD_ENTRY_SIZE];
            memcpy(match_buf, entry, BD_ENTRY_SIZE);

            int      file_extents = 0;
            uint32_t size = 0;
            uint32_t alloc_bytes = 0;

            FileKey key = make_key(v, (const char *)match_buf, ctx.user_area);

            int rc = scan_extents(key, 0, &file_extents, &size, &alloc_bytes);

            if (rc != EOK)
                return rc;

            out->size = size;
            out->alloc_bytes = alloc_bytes;
            out->extents = (uint16_t)file_extents;
            out->attrib = match_buf[BD_DIR_ATTR];
            out->user_area = match_buf[BD_DIR_USER];
            strcpy(out->name, name);

            return i + 1;
        }
    }

    return ENOENT;
}

/*
 * Create a new empty file.  Returns EEXIST if the name already
 * exists, EDIRFULL if the root directory is full.
 */
int bd_create(const char *n83, FsContext ctx)
{
    CHECK_VOL(v, ctx.vol_id);

    if (v->read_only)
        return EVOLRO;

    int fd = fcb_alloc();

    if (fd < 0)
        return ENFILE;

    FCB *f = &g_bd.fcb[fd];
    int  fidx = -1;

    for (uint16_t s = 0; s < BD_ROOT_SECS; s++)
    {
        if (vol_read(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
        {
            f->in_use = 0;
            return EIO;
        }

        for (uint16_t e = 0; e < BD_ENTRIES_PER_SEC; e++)
        {
            uint16_t i = s * BD_ENTRIES_PER_SEC + e;

            if (i >= BD_ROOT_ENTRIES)
                goto create_done;

            uint8_t *entry = g_bd.sec_buf + e * BD_ENTRY_SIZE;

            if (entry[0] == BD_ENTRY_EMPTY || entry[0] == BD_ENTRY_DELETED)
            {
                if (fidx == -1)
                    fidx = i;

                continue;
            }

            if (entry[BD_DIR_USER] == ctx.user_area && name83_match(entry, n83))
            {
                f->in_use = 0;
                return EEXIST;
            }
        }
    }

create_done:

    if (fidx == -1)
    {
        f->in_use = 0;
        return EDIRFULL;
    }

    uint8_t *entry = load_dir_entry(v, (uint16_t)fidx);

    if (!entry)
    {
        f->in_use = 0;
        return EIO;
    }

    memset(entry, 0, BD_ENTRY_SIZE);
    memcpy(entry, n83, NAME83_LEN);
    entry[BD_DIR_ATTR] = 0;
    entry[BD_DIR_USER] = ctx.user_area;

    if (vol_write(v, v->root_start_sec + (uint16_t)fidx / BD_ENTRIES_PER_SEC, g_bd.sec_buf) != EOK)
    {
        f->in_use = 0;
        return EIO;
    }

    memcpy(f->name83, n83, NAME83_LEN);

    fcb_init(f, ctx, 0, 1, (uint16_t)fidx);

    f->cur.attrib = 0;
    memset(f->cur.blocks, 0, sizeof(f->cur.blocks));

    return fd;
}

/*
 * Delete a file.  Returns EPERM if the file is read-only.
 */
int bd_delete(const char *name83, FsContext ctx)
{
    CHECK_VOL(v, ctx.vol_id);

    if (v->read_only)
        return EVOLRO;

    FileKey key = make_key(v, name83, ctx.user_area);

    DirInfo di;

    if (find_extent(key, 0, &di) != EOK)
        return ENOENT;

    if (di.attrib & FILE_ATTR_READ_ONLY)
        return EPERM;

    for (uint16_t ei = 0; ei <= UINT8_MAX; ei++)
    {
        if (find_extent(key, (uint8_t)ei, &di) != EOK)
            break;

        uint8_t *entry = load_dir_entry(v, di.diridx);

        if (!entry)
            return EIO;

        uint16_t blocks[BD_BLOCKS_PER_EXTENT];
        entry_block_list(entry, blocks);

        /*
         * Transaction order: persist the deletion before releasing any
         * allocator state.  A crash between the two steps can only leak
         * blocks (reclaimed at the next rescan), never hand live data
         * out for reuse.
         */
        entry[0] = BD_ENTRY_DELETED;

        if (vol_write(v, v->root_start_sec + di.diridx / BD_ENTRIES_PER_SEC, g_bd.sec_buf) != EOK)
            return EIO;

        for (int b = 0; b < BD_BLOCKS_PER_EXTENT; b++)
        {
            if (blocks[b])
                free_block(v, blocks[b]);
        }
    }

    return EOK;
}

/*
 * Rename a file.  Returns EEXIST if new83 is already taken.
 * No data blocks are moved.
 */
int bd_rename(const char *old83, const char *new83, FsContext ctx)
{
    CHECK_VOL(v, ctx.vol_id);

    if (v->read_only)
        return EVOLRO;

    int num_extents;

    int rc = scan_extents(make_key(v, new83, ctx.user_area), 0, &num_extents, 0, 0);

    if (rc != EOK)
        return rc;

    if (num_extents > 0)
        return EEXIST;

    int fnd = 0;

    for (uint16_t s = 0; s < BD_ROOT_SECS; s++)
    {
        int dirty = 0;
        int stop = 0;

        if (vol_read(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
            return EIO;

        for (uint16_t e = 0; e < BD_ENTRIES_PER_SEC; e++)
        {
            uint16_t i = s * BD_ENTRIES_PER_SEC + e;

            if (i >= BD_ROOT_ENTRIES)
            {
                stop = 1;
                break;
            }

            uint8_t *entry = g_bd.sec_buf + e * BD_ENTRY_SIZE;

            if (entry[0] == BD_ENTRY_EMPTY)
            {
                stop = 1;
                break;
            }

            if (entry[0] == BD_ENTRY_DELETED || entry[BD_DIR_USER] != ctx.user_area)
                continue;

            if (!name83_match(entry, old83))
                continue;

            memcpy(entry, new83, NAME83_LEN);
            dirty = 1;
            fnd = 1;
        }

        if (dirty && vol_write(v, v->root_start_sec + s, g_bd.sec_buf) != EOK)
            return EIO;

        if (stop)
            break;
    }

    return fnd ? EOK : ENOENT;
}

uint32_t bd_size(int fd)
{
    FCB *f = fcb_get(fd);

    return f ? f->size : 0;
}

/*
 * Set attributes on all extents of a file.  Returns ENOENT if the
 * file does not exist.
 */
int bd_fsetattr(const char *name83, FsContext ctx, uint8_t attrib)
{
    CHECK_VOL(v, ctx.vol_id);

    /* The extent index is a uint8_t on disk, so at most 256 extents exist. */
    int rc = ENOENT;

    for (uint16_t ei = 0; ei <= UINT8_MAX; ei++)
    {
        DirInfo di;

        int frc = find_extent(make_key(v, name83, ctx.user_area), (uint8_t)ei, &di);

        if (frc == ENOENT)
            break;

        if (frc != EOK)
            return frc;

        uint8_t *entry = load_dir_entry(v, di.diridx);

        if (!entry)
            return EIO;

        entry[BD_DIR_ATTR] = attrib;

        if (vol_write(v, v->root_start_sec + di.diridx / BD_ENTRIES_PER_SEC, g_bd.sec_buf) != EOK)
            return EIO;

        rc = EOK;
    }

    return rc;
}