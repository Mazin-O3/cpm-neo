/*
 * core/ccp/cmd_files.c — ERA, REN, TYPE command implementations
 *
 * ERA removes files (supports wildcards).
 * REN renames files (supports wildcard pattern substitution).
 * TYPE displays a file on the console with pagination.
 */

#include "ccp.h"
#include <stdio.h>
#include <string.h>

static const char *era_fmt = "f*";    /* ERA filespec */
static const char *ren_fmt = "f* p*"; /* REN old new */
static const char *type_fmt = "f";    /* TYPE filespec */

/*
 * RenMatch: holds the result of matching a source wildcard pattern against
 * a concrete filename.  nseg/seg_start/seg_len capture '*' segments (variable
 * parts); pos_map holds the characters consumed by successive '?' wildcards,
 * replayed in order by ren_pattern_format().
 */
#define REN_MAX_SEG 8
#define REN_MAX_POS 32

typedef struct
{
    const char *seg_start[REN_MAX_SEG];
    int         seg_len[REN_MAX_SEG];
    int         nseg;
    char        pos_map[REN_MAX_POS];
    int         npos;
} RenMatch;

/*
 * ren_pattern_match — Match a source wildcard pattern (e.g. "F*.C")
 * against a concrete filename, capturing '*' and '?' segments into rm.
 * Used by REN to extract the variable parts of the matched name.
 */
static void ren_pattern_match(const char *src_pat, const char *matched, RenMatch *rm)
{
    rm->nseg = 0;
    rm->npos = 0;

    const char *sp = src_pat;
    const char *mp = matched;

    while (*sp && *mp)
    {
        if (*sp == '*')
        {
            sp++;

            while (*sp == '*' || *sp == '?')
                sp++;

            if (*sp == '\0')
            {
                if (rm->nseg < REN_MAX_SEG)
                {
                    rm->seg_start[rm->nseg] = mp;
                    rm->seg_len[rm->nseg] = (int)strlen(mp);
                    rm->nseg++;
                }

                break;
            }

            const char *found = strchr(mp, *sp);

            if (!found)
                break;

            if (rm->nseg < REN_MAX_SEG)
            {
                rm->seg_start[rm->nseg] = mp;
                rm->seg_len[rm->nseg] = (int)(found - mp);
                rm->nseg++;
            }

            mp = found;
        }
        else
        {
            if (*sp == '?')
            {
                if (rm->npos < REN_MAX_POS)
                    rm->pos_map[rm->npos++] = *mp;
                sp++;
                mp++;
            }
            else if (*sp == *mp)
            {
                sp++;
                mp++;
            }
            else
            {
                break;
            }
        }
    }
}

/*
 * ren_pattern_format — Substitute captured segments back into a
 * destination wildcard pattern to produce the new filename.
 * E.g. matched="FOO.C" with dst="X*" produces "XOO.C".
 */
static void ren_pattern_format(const char *dst_pat, const RenMatch *rm, char *out, int out_sz)
{
    int seg_idx = 0, q_idx = 0, o = 0;

    for (const char *dp = dst_pat; *dp && o < out_sz - 1; dp++)
    {
        if (*dp == '*')
        {
            if (seg_idx < rm->nseg)
            {
                int         len = rm->seg_len[seg_idx];
                const char *s = rm->seg_start[seg_idx];

                for (int i = 0; i < len && o < out_sz - 1; i++)
                    out[o++] = s[i];
                seg_idx++;
            }
        }
        else if (*dp == '?')
        {
            /* '?' consumes captured characters in match order, not by
             * absolute position in the destination pattern. */

            if (q_idx < rm->npos)
                out[o++] = rm->pos_map[q_idx];
            q_idx++;
        }
        else
        {
            out[o++] = *dp;
        }
    }

    out[o] = '\0';
}

CmdErr cmd_era(FsContext *ctx, int argc, char **argv)
{
    if (!check_fmt(argc, argv, era_fmt))
        return cmderr_syntax(NULL);

    if (!has_wildcard(argv[1]))
    {
        int rc = remove(argv[1]);

        return cmderr_bdos(vol_from_arg(argv[1], ctx->vol_id), rc);
    }

    FileRef ref;

    if (!parse_fileref(ctx, argv[1], &ref))
        return cmderr_syntax(NULL);

    FileInfo di;

    int found = 0;

    while (find_next(argv[1], &di) == EOK)
    {
        char full[FSPATH_MAX];
        make_path(full, ref.fs_ctx, di.name);
        int rc = remove(full);

        if (rc != EOK)
            return cmderr_bdos(ref.fs_ctx.vol_id, rc);

        found = 1;
    }

    if (!found)
        return cmderr_errno(ENOENT);

    return cmderr_ok();
}

CmdErr cmd_ren(FsContext *ctx, int argc, char **argv)
{
    if (!check_fmt(argc, argv, ren_fmt))
        return cmderr_syntax(NULL);

    if (!has_wildcard(argv[1]))
    {
        FileRef src, dst;

        if (!parse_fileref(ctx, argv[1], &src))
            return cmderr_syntax(NULL);

        if (!parse_fileref(ctx, argv[2], &dst))
            return cmderr_syntax(NULL);

        const char *dn = dst.name[0] ? dst.name : src.name;
        char        src_full[FSPATH_MAX], dst_full[FSPATH_MAX];
        make_path(src_full, src.fs_ctx, src.name);
        make_path(dst_full, dst.fs_ctx, dn);

        int rc = rename(src_full, dst_full);

        return cmderr_bdos(src.fs_ctx.vol_id, rc);
    }

    FileRef src;

    if (!parse_fileref(ctx, argv[1], &src))
        return cmderr_syntax(NULL);

    FileRef dst;

    if (!parse_fileref(ctx, argv[2], &dst))
        return cmderr_syntax(NULL);

    const char *src_pat = strchr(argv[1], ':');
    src_pat = src_pat ? src_pat + 1 : argv[1];
    const char *dst_pat =
        dst.name[0] ? (strchr(argv[2], ':') ? strchr(argv[2], ':') + 1 : argv[2]) : src_pat;

    FileInfo di;
    int      total = 0;

    while (find_next(argv[1], &di) == EOK)
    {
        char     new_name[FILENAME_MAX];
        RenMatch rm;
        ren_pattern_match(src_pat, di.name, &rm);
        ren_pattern_format(dst_pat, &rm, new_name, sizeof(new_name));

        char src_full[FSPATH_MAX], dst_full[FSPATH_MAX];
        make_path(src_full, src.fs_ctx, di.name);
        make_path(dst_full, dst.fs_ctx, new_name);
        int rc = rename(src_full, dst_full);

        if (rc != EOK)
            return cmderr_bdos(src.fs_ctx.vol_id, rc);
        total++;
    }

    if (total == 0)
        return cmderr_errno(ENOENT);

    return cmderr_ok();
}

CmdErr cmd_type(FsContext *ctx, int argc, char **argv)
{
    if (!check_fmt(argc, argv, type_fmt))
        return cmderr_syntax(NULL);

    int fd = open(argv[1], "r");

    if (fd < 0)
        return cmderr_bdos(vol_from_arg(argv[1], ctx->vol_id), fd);

    Pager pg = pager_start();

    uint8_t buf[128];
    int     n, col = 0, stop = 0;

    while (!stop && (n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n && !stop; i++)
        {
            uint8_t c = buf[i];

            if (c == CH_EOF)
            {
                stop = 1;
                break;
            }

            if (c >= 0x20 || c == '\n' || c == '\t')
            {
                putchar((char)c);

                int is_newline = (c == '\n');

                if (c >= 0x20 && ++col >= pg.cols)
                    is_newline = 1;

                if (is_newline)
                {
                    if (pager_line(&pg))
                        stop = 1;
                    col = 0;
                }
            }
        }
    }

    if (n < 0)
    {
        putchar('\n');
        close(fd);
        return cmderr_errno(EIO);
    }

    putchar('\n');
    close(fd);
    return cmderr_ok();
}
