/*
 * apps/sys/stat.c — STAT command implementation
 *
 * STAT with no arguments shows volume stats for the current volume.
 * STAT DSK: shows all volumes' statistics.
 * STAT filespec shows file sizes and access status.
 */

#include <ccplib.h>
#include <stdio.h>
#include <string.h>

static const char *stat_vol_fmt = "v";   /* STAT B: — Volume stats, no user digits */
static const char *stat_file_fmt = "f*"; /* STAT FOO.TXT — File stats */

static CmdErr stat_vol_single(int8_t vol)
{
    VolStat ds;

    int rc = vstat(vol, &ds);

    if (rc != EOK)
        return cmderr_bdos(vol, rc);

    printf("%c: ", 'A' + vol);

    const char *mode = ds.read_only ? "RO" : "RW";

    printf("%s, Free: %uK\n", mode, ds.free_blocks);

    return cmderr_ok();
}

static CmdErr stat_vol(int8_t vol)
{
    VolStat ds;

    int rc = vstat(vol, &ds);

    if (rc != EOK)
        return cmderr_bdos(vol, rc);

    printf("Bytes Remaining on %c: ", 'A' + vol);

    printf("%uK\n", ds.free_blocks);

    return cmderr_ok();
}

static void stat_file(int8_t vol_id, FileInfo *di)
{
    uint16_t secs = (uint16_t)((di->size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE);
    uint16_t kb = (uint16_t)(di->alloc_bytes / 1024);

    const char *cls = (di->attrib & FILE_ATTR_SYSTEM) ? "Sys" : "Dir";
    const char *acc = (di->attrib & FILE_ATTR_READ_ONLY) ? "RO" : "RW";

    SplitName sn = split_name83(di->name);

    char base[NAME83_BASE + 1], ext[NAME83_EXT + 1];
    pad_field(base, sn.base, sn.base_len, NAME83_BASE); /* Left-justified in 8 */
    pad_field(ext, sn.ext, sn.ext_len, sn.ext_len);     /* NUL-copy only      */

    printf(" %4u%6uK%5u %s %-2s         %c:%s.%s\n", secs, kb, di->extents, cls, acc, 'A' + vol_id,
           base, ext);
}

static CmdErr stat_dsk(void)
{
    SysInfo si;

    if (sys_info(&si) != EOK)
        return cmderr_bdos(0, EIO);

    VolStat vs[MAX_VOLUMES];
    uint8_t vol_stat_ok[MAX_VOLUMES] = {0};

    uint16_t disk_usable_k = 0;

    for (int8_t v = 0; v < MAX_VOLUMES; v++)
    {
        if (!si.vol_mounted[v])
            continue;

        if (vstat(v, &vs[v]) != EOK)
            continue;

        vol_stat_ok[v] = 1;
        disk_usable_k += vs[v].total_blocks;
    }

    printf("\n");
    printf("%-16s : %uK\n", "Total", si.disk_size_kb);
    printf("%-16s : %uK\n", "Usable", disk_usable_k);
    printf("%-16s : %uK\n", "Unallocated", si.disk_unalloc_kb);

    printf("--------------------------\n");

    printf("Volume  Mode  Used  Total\n");

    for (int8_t v = 0; v < MAX_VOLUMES; v++)
    {
        if (!si.vol_mounted[v] || !vol_stat_ok[v])
            continue;

        const char *mode = vs[v].read_only ? "RO" : "RW";
        uint16_t    vol_used_k = vs[v].total_blocks - vs[v].free_blocks;

        printf("%c:      %s%7uK%6uK\n", 'A' + v, mode, vol_used_k, vs[v].total_blocks);
    }

    printf("\n");

    return cmderr_ok();
}

static CmdErr cmd_stat(FsContext *ctx, int argc, char **argv)
{
    if (argc >= 2)
    {
        if (check_fmt(argc, argv, "DSK:"))
            return stat_dsk();

        if (!check_fmt(argc, argv, stat_vol_fmt) && !check_fmt(argc, argv, stat_file_fmt))
            return cmderr_syntax(NULL);
    }

    char full_pat[FSPATH_MAX];

    int8_t vol_id = ctx->vol_id;

    if (argc >= 2)
    {
        FileRef ref;

        if (!parse_fileref(ctx, argv[1], &ref))
            return cmderr_syntax(NULL);

        vol_id = ref.fs_ctx.vol_id;

        if (ref.name[0])
            make_path(full_pat, ref.fs_ctx, ref.name);
        else
            full_pat[0] = '\0';
    }

    if (argc < 2)
        return stat_vol_single(vol_id);

    if (full_pat[0] == '\0')
        return stat_vol(vol_id);

    VolStat ds;

    if (vstat(vol_id, &ds) != EOK)
        return cmderr_bdos(vol_id, EIO);

    FileInfo di;

    find_reset();

    if (find_next(full_pat, &di) != EOK)
        return cmderr_errno(ENOENT);

    printf(" Secs  Bytes  Ext Attributes      Name\n");

    do
    {
        stat_file(vol_id, &di);
    } while (find_next(full_pat, &di) == EOK);

    putchar('\n');

    return stat_vol(vol_id);
}

int main(int argc, char **argv)
{
    return ccp_run_app(cmd_stat, argc, argv);
}
