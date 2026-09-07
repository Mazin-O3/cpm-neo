/*
 * apps/sys/copy.c — COPY command implementation
 *
 * Supports single-file and wildcard copy between volumes/users.
 * Format:  COPY FOO.TXT B:        (copy to B: with same name)
 *          COPY A:FOO.TXT B:      (explicit source volume)
 *          COPY F*.COM B:         (wildcard copy)
 *
 * The destination name is used as-is for single-file copies; for
 * wildcard copies, only non-wildcard destinations are treated as a
 * rename target.
 */

#include <ccplib.h>
#include <string.h>

/*
 * copy_one — Copy a single file from src to dst.
 * Preserves the source file's attributes on the destination.
 * Sets *err_vol to the volume where the error occurred (for error reporting).
 * Returns EOK on success, or a negative error code.
 */
static int copy_one(const FileRef *src, const FileRef *dst, uint8_t src_attrib, int8_t *err_vol)
{
    char spath[FSPATH_MAX];

    make_path(spath, src->fs_ctx, src->name);

    char dpath[FSPATH_MAX];

    make_path(dpath, dst->fs_ctx, dst->name);

    /* ENOENT is reported plain (no volume prefix) by cmderr_print; any
     * other failure is attributed to the destination volume. */
    int rc = fcopy(dpath, spath);

    if (rc != EOK && rc != ENOENT)
        *err_vol = dst->fs_ctx.vol_id;

    if (rc != EOK)
        return rc;

    rc = fsetattr(dpath, src_attrib);

    if (rc != EOK)
    {
        *err_vol = dst->fs_ctx.vol_id;
        return rc;
    }

    return EOK;
}

static CmdErr cmd_copy(FsContext *ctx, int argc, char **argv)
{
    if (argc < 2 || (argc > 2 && !check_fmt(argc, argv, "f* p*")))
        return cmderr_syntax(NULL);

    FileRef src, dst;

    char src_pat[FSPATH_MAX];

    if (argc == 3)
    {
        if (!parse_fileref(ctx, argv[1], &src) || !parse_fileref(ctx, argv[2], &dst))
            return cmderr_syntax(NULL);
    }
    else
    {
        if (strchr(argv[1], ':'))
        {
            if (!parse_fileref(ctx, argv[1], &dst))
                return cmderr_syntax(NULL);

            src.fs_ctx = *ctx;

            strncpy(src.name, dst.name, sizeof(src.name));
        }
        else
        {
            if (!parse_fileref(ctx, argv[1], &src))
                return cmderr_syntax(NULL);

            dst.fs_ctx = *ctx;

            strncpy(dst.name, src.name, sizeof(dst.name));
        }
    }

    make_path(src_pat, src.fs_ctx, src.name);

    /* The kernel uppercases all stored names, so compare insensitively:
     * "COPY foo.txt FOO.TXT" would otherwise pass this guard and the
     * create-by-rename in open("w") would destroy the source mid-copy. */

    if (src.fs_ctx.vol_id == dst.fs_ctx.vol_id && src.fs_ctx.user_area == dst.fs_ctx.user_area &&
        !strcasecmp(src.name, dst.name))
        return cmderr_syntax(NULL);

    if (!has_wildcard(src.name))
    {
        FileRef target_dst = dst;

        if (!target_dst.name[0])
            strncpy(target_dst.name, src.name, sizeof(target_dst.name));

        FileInfo sfi;

        int rc_find = find(src_pat, &sfi);

        if (rc_find != EOK)
            return cmderr_bdos(src.fs_ctx.vol_id, rc_find);

        int8_t err_vol;

        int rc = copy_one(&src, &target_dst, sfi.attrib, &err_vol);

        if (rc != EOK)
            return cmderr_bdos(err_vol, rc);

        return cmderr_ok();
    }

    if (dst.name[0] && !has_wildcard(dst.name))
        return cmderr_syntax(NULL);

    FileInfo file;

    int total = 0;

    find_reset();

    while (find_next(src_pat, &file) == EOK)
    {
        FileRef current_src = src;

        strncpy(current_src.name, file.name, sizeof(current_src.name));

        FileRef current_dst = dst;

        strncpy(current_dst.name, file.name, sizeof(current_dst.name));

        int8_t err_vol;

        int r = copy_one(&current_src, &current_dst, file.attrib, &err_vol);

        if (r != EOK)
            return cmderr_bdos(err_vol, r);

        total++;
    }

    if (total == 0)
        return cmderr_errno(ENOENT);

    return cmderr_ok();
}

int main(int argc, char **argv)
{
    return ccp_run_app(cmd_copy, argc, argv);
}
