/*
 * core/ccp/cmd_system.c — DIR, DIRS, USER, CLS, and try_implicit_run
 *
 * DIR lists directory entries with multi-column formatting.
 * DIRS lists SYS-marked files (hidden from normal DIR).
 * USER sets/displays the current user area (0-15).
 * CLS clears the console.
 * try_implicit_run attempts to exec a transient .COM program,
 * with an automatic fallback to A0: if not found on the current volume.
 */

#include "bdos.h"
#include "ccp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *dir_fmt = "p*";
static const char *user_fmt = "n";

static CmdErr dir_list(FsContext *ctx, int argc, char **argv, int show_sys)
{
    if (argc > 1 && !check_fmt(argc, argv, dir_fmt))
        return cmderr_syntax(NULL);

    char    full_pat[FSPATH_MAX];
    uint8_t vol = ctx->vol_id;
    uint8_t ua = ctx->user_area;

    if (argc >= 2)
    {
        FileRef ref;

        if (!parse_fileref(ctx, argv[1], &ref))
            return cmderr_syntax(NULL);

        vol = ref.fs_ctx.vol_id;
        ua = ref.fs_ctx.user_area;
        make_path(full_pat, (FsContext){vol, ua}, ref.name[0] ? ref.name : "*.*");
    }
    else
        make_path(full_pat, (FsContext){vol, ua}, "*.*");

    FileInfo di;

    int count = 0, col = 0, rc;

    uint8_t cw, ch;

    sys_consize(&cw, &ch);

    int ncols = cw / 20;

    if (ncols < 1)
        ncols = 1;

    find_reset();

    while ((rc = find_next(full_pat, &di)) == EOK)
    {
        int show_file = ((di.attrib & FILE_ATTR_SYSTEM) != 0) == show_sys;

        if (!show_file)
        {
            continue;
        }

        if (col == 0)
        {
            if (ua)
                printf("%c%u: ", 'A' + vol, ua);
            else
                printf("%c: ", 'A' + vol);
        }
        else
        {
            printf(" : ");
        }

        SplitName sn = split_name83(di.name);
        char      base[NAME83_BASE + 1], ext[NAME83_EXT + 1];
        pad_field(base, sn.base, sn.base_len, NAME83_BASE);
        pad_field(ext, sn.ext, sn.ext_len, NAME83_EXT);
        printf("%s %s", base, ext);

        count++;
        col++;

        if (col >= ncols)
        {
            printf("\n");
            col = 0;
        }
    }

    if (col != 0)
        printf("\n");

    if (count == 0)
        return cmderr_errno(ENOENT);

    if (rc != ENOENT)
        return cmderr_bdos(vol, EIO);

    return cmderr_ok();
}

CmdErr cmd_dir(FsContext *ctx, int argc, char **argv)
{
    return dir_list(ctx, argc, argv, 0);
}

CmdErr cmd_dirs(FsContext *ctx, int argc, char **argv)
{
    return dir_list(ctx, argc, argv, 1);
}

CmdErr cmd_user(FsContext *ctx, int argc, char **argv)
{
    if (argc == 1)
    {
        printf("User: %u\n", ctx->user_area);
        return cmderr_ok();
    }

    if (!check_fmt(argc, argv, user_fmt))
        return cmderr_syntax(NULL);

    int ua;

    if (!parse_int(argv[1], &ua))
        return cmderr_syntax(NULL);

    if (ua < 0 || ua > USER_AREA_MAX)
        return cmderr_syntax(argv[1]);

    int rc = ccp_setuser(ctx, ua);

    if (rc != EOK)
        return cmderr_bdos(ctx->vol_id, rc);

    return cmderr_ok();
}

/*
 * exec_if_sys — If the $SYS-marked .COM at |path| exists, execute it.
 * Returns 1 if the file was found (exec attempted, *out_rc set), 0 otherwise.
 */
static int exec_if_sys(const char *path, int argc, char **argv, int *out_rc)
{
    FileInfo fi;

    if (find(path, &fi) != EOK)
        return 0;

    if (!(fi.attrib & FILE_ATTR_SYSTEM))
        return 0;

    *out_rc = exec(path, argc, argv);

    return 1;
}

/*
 * try_implicit_run — Attempt to execute a transient command.
 * If the name has no extension, ".COM" is tried implicitly.
 * Bare commands (no explicit prefix) search in this order:
 *   1. current drive / current user area (any file)
 *   2. current drive / user 0 ($SYS files only)
 *   3. A: / user 0 ($SYS files only)
 * Returns cmderr_ok() on ENOENT (unknown command, not an error).
 */
CmdErr try_implicit_run(FsContext *ctx, int argc, char **argv)
{
    int n = strlen(argv[0]);

    if (n >= 4 && argv[0][n - 4] == '.')
    {
        if (strcasecmp(argv[0] + n - 3, "COM") != 0)
            return cmderr_ok();
    }

    int rc = exec(argv[0], argc, argv);

    if (rc == ENOENT && !strchr(argv[0], ':'))
    {
        char name[FILENAME_MAX];

        name_copy(name, argv[0], sizeof(name) - 1);

        if (!strchr(name, '.'))
            strncat(name, ".COM", sizeof(name) - strlen(name) - 1);

        char cand[ARG_LEN_MAX + 8];

        snprintf(cand, sizeof(cand), "%c0:%s", 'A' + ctx->vol_id, name);

        if (!exec_if_sys(cand, argc, argv, &rc) && ctx->vol_id != VOL_A)
        {
            snprintf(cand, sizeof(cand), "A0:%s", name);

            exec_if_sys(cand, argc, argv, &rc);
        }
    }

    if (rc == ENOENT)
        return cmderr_ok();

    if (rc == ENOEXEC)
        return cmderr_errno(ENOEXEC);

    return cmderr_bdos(vol_from_arg(argv[0], ctx->vol_id), rc);
}

CmdErr cmd_cls(FsContext *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argc;
    (void)argv;

    if (argc > 1)
        return cmderr_syntax(NULL);

    putchar('\f');

    return cmderr_ok();
}

CmdErr cmd_echo(FsContext *ctx, int argc, char **argv)
{
    (void)ctx;

    for (int i = 1; i < argc; i++)
        printf(i > 1 ? " %s" : "%s", argv[i]);

    putchar('\n');

    return cmderr_ok();
}

/*
 * SYNC — Enforce durability of all pending writes. Falls through to the
 * kernel sync chain (sys_sync -> bd_sync -> disk_sync -> bios_sync), which
 * commits the disk-layer cache and requests a platform persistence barrier.
 */
CmdErr cmd_sync(FsContext *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argv;

    if (argc > 1)
        return cmderr_syntax(NULL);

    int rc = sync();

    if (rc != EOK)
        return cmderr_errno(rc);

    printf("OK\n");
    return cmderr_ok();
}
