/*
 * sysgen/src/cmd_apps.c — Bundled-app build & install
 *
 * Compiles a source folder to a .com via app_build.sh and adds it to the
 * disk.  Shared by `sysgen new` (seeds a fresh image) and `sysgen install
 * --sys-apps/--extra-apps` (fills an existing image).
 */

#include "cmd.h"
#include "bdos.h"
#include "disk.h"
#include "sysgen.h"
#include "utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Walk-state for recursively installing bundled apps. */
typedef struct
{
    const SysgenPaths *paths;
    const AddFileOpts *opts;
    int failed;
} BundledScan;

int build_folder_com(const SysgenPaths *paths, const char *src, const BuildFolderOpts *opts)
{
    char platform_path[SYSGEN_FULL_PATH_MAX];
    snprintf(platform_path, sizeof(platform_path), "%s/.platform_dir", paths->build_dir);

    FILE *f = fopen(platform_path, "r");

    if (!f)
    {
        err("no system build found in '%s' -- run 'sysgen new' first", paths->build_dir);
        return 1;
    }

    if (fgets(opts->platform, opts->platform_n, f) == NULL)
        opts->platform[0] = '\0';
    fclose(f);

    opts->platform[strcspn(opts->platform, "\r\n")] = '\0';

    if (!*opts->platform)
    {
        err("missing platform record '%s' -- re-run 'sysgen new'", platform_path);
        return 1;
    }

    size_t len = strlen(src);

    while (len > 0 && (src[len - 1] == '/' || src[len - 1] == '\\'))
        len--;

    if (len == 0 || len >= SYSGEN_FULL_PATH_MAX)
    {
        err("invalid or path too long: '%s'", src);
        return 1;
    }

    char dirbuf[SYSGEN_FULL_PATH_MAX];
    memcpy(dirbuf, src, len);
    dirbuf[len] = '\0';

    /* App name: folder basename, or a single source file's basename minus
     * its extension (mycmd.c -> mycmd.com -> MYCMD.COM). */
    const char *base;
    char basebuf[SYSGEN_FULL_PATH_MAX];

    if (!dir_exists(dirbuf))
    {
        snprintf(basebuf, sizeof(basebuf), "%s", extract_basename(dirbuf));
        char *dot = strrchr(basebuf, '.');

        if (dot && dot != basebuf)
            *dot = '\0';
        base = basebuf;
    }
    else
    {
        base = extract_basename(dirbuf);
    }

    if (!*base)
    {
        err("cannot derive app name from '%s'", src);
        return 1;
    }

    snprintf(opts->out_com, opts->out_n, "%s/apps/com/%s.com", paths->build_dir, base);

    char script[SYSGEN_FULL_PATH_MAX];
    snprintf(script, sizeof(script), "%s/../app_build.sh", paths->build_dir);

    char *argv_run[] = {
        (char *)"sh", script, opts->platform, dirbuf, (char *)"-o", opts->out_com, NULL,
    };

    return spawn_and_wait(argv_run);
}

/* Build |src| (a folder or a single source file) to a .com and add it to
 * the disk.  Returns 1 on failure (also marks the scan failed). */
static int build_and_add(BundledScan *scan, const char *src)
{
    char out_com[SYSGEN_FULL_PATH_MAX + 256];
    char platform_buf[64];
    BuildFolderOpts bfo = {out_com, sizeof(out_com), platform_buf, sizeof(platform_buf)};

    if (build_folder_com(scan->paths, src, &bfo) != 0)
    {
        scan->failed = 1;
        return 1;
    }

    if (add_data_open(out_com, scan->opts) == 1)
    {
        scan->failed = 1;
        return 1;
    }

    return 0;
}

static int install_bundled_app(const char *dir, const char *name, void *ud)
{
    BundledScan *scan = ud;

    if (scan->failed)
        return 1;

    if (!dir_has_sources(dir))
    {
        printf("  Skip %s (no .c/.s/.S sources)\n", name);
        return 0;
    }

    return build_and_add(scan, dir);
}

static int install_bundled_source(const char *path, const char *name, void *ud)
{
    BundledScan *scan = ud;
    (void)name;

    if (scan->failed)
        return 1;

    return build_and_add(scan, path);
}

int install_sys_apps(const SysgenPaths *paths, const AddFileOpts *opts)
{
    BundledScan scan = {paths, opts, 0};
    char sys_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(sys_dir, sizeof(sys_dir), "%s/apps/sys", paths->root_dir);

    if (!dir_exists(sys_dir))
    {
        err("sys apps directory not found at '%s'", sys_dir);
        return 1;
    }

    printf("  \nInstalling sys apps...\n");

    for_each_source_file(sys_dir, install_bundled_source, &scan);

    return scan.failed ? 1 : 0;
}

int install_extra_apps(const SysgenPaths *paths, const AddFileOpts *opts)
{
    BundledScan scan = {paths, opts, 0};
    char extra_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(extra_dir, sizeof(extra_dir), "%s/apps/extra", paths->root_dir);

    if (!dir_exists(extra_dir))
        return 0;

    printf("  \nInstalling extra apps...\n");

    for_each_subdir(extra_dir, install_bundled_app, &scan);

    return scan.failed ? 1 : 0;
}