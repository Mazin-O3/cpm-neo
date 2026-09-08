/*
 * sysgen/src/cmd_apps.c — Bundled-app build & install
 *
 * Compiles a source folder to a .com via app_build.sh and adds it to the
 * disk.  Shared by `sysgen new` (seeds a fresh image, filtered by the
 * platform's CONFIG_SYS_APPS / CONFIG_EXTRA_APPS selection) and `sysgen
 * install <folder|file.c>` (adds one app to an existing image).
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

/* Walk-state for installing bundled apps.  `names` selects a subset of the
 * bundled apps (NULL = install all); each requested name must be found, or
 * the install fails so a typo can never silently drop an app. */
typedef struct
{
    const SysgenPaths *paths;
    const AddFileOpts *opts;
    const char *const *names;
    size_t nnames;
    int *found; /* parallel to names; 1 once a source matched */
    size_t found_count;
    int failed;
} BundledScan;

static int want_app(const BundledScan *scan, const char *name)
{
    if (!scan->names)
        return 1;

    for (size_t i = 0; i < scan->nnames; i++)
        if (strcasecmp(name, scan->names[i]) == 0)
            return 1;

    return 0;
}

static void mark_found(BundledScan *scan, const char *name)
{
    if (!scan->names)
        return;

    for (size_t i = 0; i < scan->nnames; i++)
        if (!scan->found[i] && strcasecmp(name, scan->names[i]) == 0)
        {
            scan->found[i] = 1;
            scan->found_count++;
            return;
        }
}

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

    if (!want_app(scan, name))
        return 0;

    if (!dir_has_sources(dir))
    {
        printf("  Skip %s (no .c/.s/.S sources)\n", name);
        return 0;
    }

    mark_found(scan, name);

    return build_and_add(scan, dir);
}

static int install_bundled_source(const char *path, const char *name, void *ud)
{
    BundledScan *scan = ud;

    if (scan->failed)
        return 1;

    /* App name is the file basename minus its extension (mycmd.c -> mycmd). */
    char base[SYSGEN_FULL_PATH_MAX];
    snprintf(base, sizeof(base), "%s", name);
    char *dot = strrchr(base, '.');

    if (dot && dot != base)
        *dot = '\0';

    if (!want_app(scan, base))
        return 0;

    mark_found(scan, base);

    return build_and_add(scan, path);
}

/* Report every requested name that no bundled source matched.  Returns 1 if
 * any name is missing, 0 otherwise. */
static int report_unmatched(const BundledScan *scan, const char *kind)
{
    int bad = 0;

    for (size_t i = 0; i < scan->nnames; i++)
        if (!scan->found[i])
        {
            err("%s app '%s' not found in apps/%s", kind, scan->names[i], kind);
            bad = 1;
        }

    return bad;
}

static int run_install(const SysgenPaths *paths, const AddFileOpts *opts,
                       const char *const *names, size_t nnames,
                       const char *subdir, int flat, const char *kind)
{
    int rc = 0;
    int *found = NULL;

    if (nnames == 0)
        names = NULL;

    if (names)
        found = calloc(nnames, sizeof(*found));

    BundledScan scan = {paths, opts, names, nnames, found, 0, 0};

    char apps_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(apps_dir, sizeof(apps_dir), "%s/apps/%s", paths->root_dir, subdir);

    if (dir_exists(apps_dir))
    {
        printf("  \nInstalling %s apps...\n", kind);

        if (flat)
            for_each_source_file(apps_dir, install_bundled_source, &scan);
        else
            for_each_subdir(apps_dir, install_bundled_app, &scan);
    }

    if (scan.failed || report_unmatched(&scan, kind) != 0)
        rc = 1;

    free(found);
    return rc;
}

int install_sys_apps(const SysgenPaths *paths, const AddFileOpts *opts,
                     const char *const *names, size_t nnames)
{
    return run_install(paths, opts, names, nnames, "sys", 1, "sys");
}

int install_extra_apps(const SysgenPaths *paths, const AddFileOpts *opts,
                       const char *const *names, size_t nnames)
{
    return run_install(paths, opts, names, nnames, "extra", 0, "extra");
}