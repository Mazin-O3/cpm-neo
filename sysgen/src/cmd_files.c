/*
 * sysgen/src/cmd_files.c — File operations: add, install, extract, dir
 *
 * Implementations of `sysgen add`, `sysgen install`, `sysgen extract`
 * and `sysgen dir`, plus the local helpers they share.
 */

#include "cmd.h"
#include "commands.h"

#include "bdos.h"
#include "disk.h"
#include "sysgen.h"
#include "utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Whitelists of flags accepted by each command (NULL-terminated). */
static const char *const FLAGS_FILE[] = {
    "--dst",
    "--attr",
    "--disk",
    NULL,
};
static const char *const FLAGS_DISK[] = {
    "--disk",
    NULL,
};

/* Walk-state for flat-folder add; counts added/skipped files. */
typedef struct
{
    const AddFileOpts *opts;
    int                failed;
    int                added;
    int                skipped;
} AddFolderScan;

/* Parse the shared add/install target options: the source positional, the
 * destination volume/user area, the file attributes, and the disk path.
 * When --attr is omitted, |dflt_attr| is used. */
static int parse_file_target(int argc, char **argv, const char *usage, const char **src, int *vol,
                             int *user, uint8_t *attr, uint8_t dflt_attr, char *disk_buf,
                             size_t disk_n)
{
    const char *pos[4];

    if (check_flags(argc, argv, FLAGS_FILE) != 0 || check_positionals(argc, argv, 2, 2) != 0)
        return 1;

    if (collect_positional(argc, argv, pos, 4) < 2)
    {
        err("usage: %s", usage);
        return 1;
    }

    *src = pos[1];

    resolve_disk(argc, argv, disk_buf, disk_n);

    if (parse_dst(argc, argv, vol, user) != 0)
        return 1;

    return parse_attr_dflt(argc, argv, attr, dflt_attr);
}

static int add_folder_file(const char *path, const char *name, void *ud)
{
    AddFolderScan *scan = ud;
    (void)name;

    if (scan->failed)
        return 1;

    int rc = add_data_open(path, scan->opts);

    if (rc == 1)
    {
        scan->failed = 1;
        return 1;
    }

    if (rc == 2)
        scan->skipped++;
    else
        scan->added++;

    return 0;
}

static int add_file(const char *disk, const char *file, const AddFileOpts *opts)
{
    if (open_disk(disk) != 0 || disk_init() != 0 || mount_vol((int8_t)opts->vol) != 0)
        return 1;

    if (add_data_open(file, opts) == 1)
        return 1;

    bd_sync();
    return save_disk(disk);
}

/*
 * cmd_add — Add a file or flat folder to the disk image.
 * Supports --dst=Vn for volume/user targeting, --attr for file attributes.
 * Folder mode iterates all files in the folder and skips duplicates.
 */
int cmd_add(int argc, char **argv)
{
    const char *src;
    int         vol, user;
    uint8_t     attr;
    char        disk_buf[SYSGEN_FULL_PATH_MAX];

    if (parse_file_target(
            argc, argv,
            "sysgen add <file|folder> [--dst=Vn] [--attr=RO|RW|SYS|SYS+RO] [--disk=path]", &src,
            &vol, &user, &attr, 0, disk_buf, sizeof(disk_buf)) != 0)
        return 1;

    AddFileOpts afo = {vol, user, attr, "added"};

    /* Folder mode: add every file from a flat folder, skipping duplicates. */

    if (dir_exists(src))
    {
        if (dir_has_subdirs(src))
        {
            err("'%s' contains subdirectories; 'sysgen add' requires a flat folder", src);
            return 1;
        }

        if (open_disk(disk_buf) != 0 || disk_init() != 0 || mount_vol((int8_t)vol) != 0)
            return 1;

        AddFolderScan scan = {&afo, 0, 0, 0};
        for_each_flat_file(src, add_folder_file, &scan);

        if (scan.failed)
            return 1;

        bd_sync();

        if (save_disk(disk_buf) != 0)
            return 1;

        printf("\nAdded %d file(s) to %c:%u (%d already existed)\n", scan.added, 'A' + vol, user,
               scan.skipped);
        return 0;
    }

    if (!file_exists(src))
    {
        err("'%s' not found", src);
        return 1;
    }

    return add_file(disk_buf, src, &afo);
}

/*
 * cmd_install — Compile a folder/file.c to .COM and add it to the image.
 */
int cmd_install(int argc, char **argv)
{
    const char *src;
    int         vol, user;
    uint8_t     attr;
    char        disk_buf[SYSGEN_FULL_PATH_MAX];

    if (parse_file_target(
            argc, argv,
            "sysgen install <folder|file.c> [--dst=Vn] [--attr=RO|RW|SYS|SYS+RO] [--disk=path]",
            &src, &vol, &user, &attr, FILE_ATTR_READ_ONLY, disk_buf, sizeof(disk_buf)) != 0)
        return 1;

    if (!dir_exists(src) && !file_exists(src))
    {
        err("'%s' not found; source must be a folder or a .c/.s/.S file", src);
        return 1;
    }

    if (file_exists(src) && !has_source_ext(src))
    {
        err("'%s' is not a .c/.s/.S source file", src);
        return 1;
    }

    char            out_com[SYSGEN_PATH_MAX + 256];
    char            platform[64];
    BuildFolderOpts bfo = {out_com, sizeof(out_com), platform, sizeof(platform)};

    if (build_folder_com(sysgen_paths(), src, &bfo) != 0)
        return 1;

    AddFileOpts afo = {vol, user, attr, "installed"};
    return add_file(disk_buf, out_com, &afo);
}

/*
 * cmd_extract — Extract all files from every volume/user into a flat folder.
 * Works even on damaged images (loads the raw image buffer without
 * validating the boot sector, then uses bd_* for file I/O).
 */
int cmd_extract(int argc, char **argv)
{
    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 1, 1) != 0)
        return 1;

    char disk_buf[SYSGEN_FULL_PATH_MAX];
    resolve_disk(argc, argv, disk_buf, sizeof(disk_buf));

    /* Load the image without validating the boot sector: extraction must work
     * even when the system area is damaged. */
    uint8_t *buf = NULL;
    uint32_t len = 0;

    if (read_file(disk_buf, &buf, &len) != 0)
    {
        err("cannot read '%s'", disk_buf);
        return 1;
    }

    if (len < DISK_SECTOR_SIZE || (len % DISK_SECTOR_SIZE) != 0)
    {
        free(buf);
        err("'%s' is not a valid disk image", disk_buf);
        return 1;
    }

    sysgen_set_disk(buf, len);

    if (disk_init() != 0)
    {
        err("disk_init failed: '%s' has no valid volume map", disk_buf);
        free(buf);
        return 1;
    }

    const SysgenPaths *paths = sysgen_paths();
    char               out_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(out_dir, sizeof(out_dir), "%s/extract", paths->build_dir);

    if (mkdir_p(out_dir) != 0)
    {
        err("cannot create extract directory '%s'", out_dir);
        free(buf);
        return 1;
    }

    int total = 0, skipped = 0, errors = 0;

    for (int8_t v = 0; v < MAX_VOLUMES; v++)
    {
        if (bd_bind((int8_t)v) != EOK)
            continue;

        for (uint8_t u = 0; u <= USER_AREA_MAX; u++)
        {
            FsContext ctx = {(int8_t)v, u};
            FileInfo  fi;
            char      allpat[NAME83_LEN + 1] = "***********"; /* 8 base + 3 ext */
            uint16_t  resume = 0;
            int       rc;

            while ((rc = bd_find(allpat, ctx, &fi, resume)) > 0)
            {
                resume = (uint16_t)rc;

                char path[SYSGEN_FULL_PATH_MAX + 256];
                snprintf(path, sizeof(path), "%s/%s", out_dir, fi.name);

                if (file_exists(path))
                {
                    printf("  skip %c:%u %s (already extracted)\n", 'A' + v, u, fi.name);
                    skipped++;
                    continue;
                }

                char n83[NAME83_LEN + 1];
                to_name83(fi.name, n83);
                n83[NAME83_LEN] = '\0';

                int fd = bd_open(n83, ctx, 0);

                if (fd < 0)
                {
                    printf("  error opening %c:%u %s (%s)\n", 'A' + v, u, fi.name, err_str(fd));
                    errors++;
                    continue;
                }

                uint8_t *data = NULL;
                uint32_t size = 0, cap = 0;
                uint8_t  chunk[1024];
                int      r;

                while ((r = bd_read(fd, chunk, sizeof(chunk))) > 0)
                {
                    if (size + (uint32_t)r > cap)
                    {
                        cap = cap ? cap * 2 : 4096;

                        while (cap < size + (uint32_t)r)
                            cap *= 2;
                        data = realloc(data, cap);
                    }

                    memcpy(data + size, chunk, (size_t)r);
                    size += (uint32_t)r;
                }

                bd_close(fd);

                if (r < 0)
                {
                    free(data);
                    printf("  error reading %c:%u %s\n", 'A' + v, u, fi.name);
                    errors++;
                    continue;
                }

                if (write_file(path, data, size) != 0)
                {
                    free(data);
                    printf("  error writing '%s'\n", path);
                    errors++;
                    continue;
                }

                free(data);

                char h[24];
                hr(h, sizeof(h), size);
                printf("  %c:%u %-13s %9s  -> %s\n", 'A' + v, u, fi.name, h, path);
                total++;
            }
        }
    }

    free(buf);
    printf("\nExtracted %d file(s) to '%s' (%d skipped, %d errors)\n", total, out_dir, skipped,
           errors);
    return errors ? 1 : 0;
}

/*
 * cmd_dir — List files on the host-side disk image (like the CCP's DIR
 * but operates on the raw image file).
 */
int cmd_dir(int argc, char **argv)
{
    const char *pos[3];

    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 1, 2) != 0)
        return 1;

    int npos = collect_positional(argc, argv, pos, 3);

    ImageTarget tgt;

    if (setup_disk_target(argc, argv, (npos > 1) ? pos[1] : NULL, &tgt) != 0)
        return 1;

    FileInfo fi;
    uint16_t resume = 0;
    int      count = 0;
    int      rc = 0;
    char     allpat[NAME83_LEN + 1] = "***********"; /* 8 base + 3 ext */

    while ((rc = bd_find(allpat, tgt.ctx, &fi, resume)) > 0)
    {
        const char *sys = (fi.attrib & FILE_ATTR_SYSTEM) ? "  [SYS]" : "";
        char        h[24];
        hr(h, sizeof(h), fi.size);
        printf("  %-13s %9s%s\n", fi.name, h, sys);
        count++;
        resume = (uint16_t)rc;
    }

    if (rc != ENOENT)
    {
        err("dir: scan failed (%s)", err_str(rc));
        return 1;
    }

    printf("  %d file(s)\n", count);
    return 0;
}