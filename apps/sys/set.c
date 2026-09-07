/*
 * apps/sys/set.c — SET command implementation
 *
 * SET B: RO         — Set volume attribute (read-only)
 * SET B: RW         — Clear volume attribute
 * SET B: MT         — Mount (format + bind) a volume
 * SET B: UM         — Unmount a volume (must be empty)
 * SET B: RZ +n      — Grow a volume by n KB
 * SET B: RZ -n      — Shrink a volume by n KB
 * SET FOO.TXT RO    — Set file attribute
 * SET FOO.TXT SYS   — Mark as system file
 *
 * Volume vs file is disambiguated by parse_fileref: if name[0] is '\0'
 * after parsing, the argument was a bare volume+colon (volume command).
 */

#include <ccplib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Volume subcommands: "v" rejects user digits (B5: is invalid). */
static const char *set_vol_fmt = "v a";     /* SET B: RO, MT, UM */
static const char *set_vol_n_fmt = "v a n"; /* SET B: RZ 10, RZ -5 */

/* File attribute: "f" accepts optional user digits (B5:FOO.TXT). */
static const char *set_file_attr_fmt = "f a"; /* SET FOO.TXT RO */

static void print_vol_size(int8_t vol_id)
{
    VolStat ds;

    if (vstat(vol_id, &ds) == EOK)
        printf("%c: %uK\n", 'A' + vol_id, ds.total_blocks);
}

static CmdErr set_mount(int8_t vol_id)
{
    int rc = mount(vol_id);

    if (rc != EOK)
        return cmderr_bdos(vol_id, rc);

    print_vol_size(vol_id);
    return cmderr_ok();
}

static CmdErr set_resize(int8_t vol_id, char *arg)
{
    int n;

    if (!parse_int(arg, &n) || n > (int)INT16_MAX || n < (int)INT16_MIN)
        return cmderr_bdos(vol_id, EINVAL);

    int rc = resize(vol_id, (int16_t)n);

    if (rc != EOK)
        return cmderr_bdos(vol_id, rc);

    print_vol_size(vol_id);
    return cmderr_ok();
}

static CmdErr set_umount(int8_t vol_id, int8_t prompt_vol_id)
{
    if (vol_id == prompt_vol_id)
        return cmderr_bdos(vol_id, EPERM);

    int rc = unmount(vol_id);

    if (rc != EOK)
        return cmderr_bdos(vol_id, rc);

    return cmderr_ok();
}

static CmdErr set_vol_attr(int8_t vol_id, const char *attrarg)
{
    uint8_t attr;

    if (strcasecmp(attrarg, "RO") == 0)
        attr = VOL_ATTR_RO;
    else if (strcasecmp(attrarg, "RW") == 0)
        attr = VOL_ATTR_RW;
    else
        return cmderr_syntax(attrarg);

    int rc = vsetattr(vol_id, attr);

    if (rc != EOK)
        return cmderr_bdos(vol_id, rc);

    return cmderr_ok();
}

static CmdErr set_matching_file_attr(FsContext ctx, const char *name, int8_t mask, int set)
{
    FileInfo di;
    int any = 0;

    find_reset();

    while (find_next(name, &di) == EOK)
    {
        char full[FSPATH_MAX];
        make_path(full, ctx, di.name);
        int8_t new_attrib = set ? (di.attrib | mask) : (di.attrib & ~mask);

        int rc = fsetattr(full, new_attrib);

        if (rc != EOK)
            return cmderr_bdos(ctx.vol_id, rc);

        any = 1;
    }

    if (!any)
        return cmderr_errno(ENOENT);

    return cmderr_ok();
}

static CmdErr set_file_attr(FsContext ctx, const char *path, const char *attrarg)
{
    if (strcasecmp(attrarg, "RO") == 0)
        return set_matching_file_attr(ctx, path, FILE_ATTR_READ_ONLY, 1);

    if (strcasecmp(attrarg, "RW") == 0)
        return set_matching_file_attr(ctx, path, FILE_ATTR_READ_ONLY, 0);

    if (strcasecmp(attrarg, "SYS") == 0)
        return set_matching_file_attr(ctx, path, FILE_ATTR_SYSTEM, 1);

    if (strcasecmp(attrarg, "DIR") == 0)
        return set_matching_file_attr(ctx, path, FILE_ATTR_SYSTEM, 0);

    return cmderr_syntax(attrarg);
}

static CmdErr cmd_set(FsContext *ctx, int argc, char **argv)
{
    if (argc < 2)
        return cmderr_syntax(NULL);

    FileRef ref;

    if (!parse_fileref(ctx, argv[1], &ref))
        return cmderr_syntax(NULL);

    int8_t vol_id = ref.fs_ctx.vol_id;
    int8_t ua = ref.fs_ctx.user_area;
    char name[FILENAME_MAX];
    name_copy(name, ref.name, sizeof(name) - 1);

    if (check_fmt(argc, argv, set_vol_n_fmt) && name[0] == '\0')
    {
        if (strcasecmp(argv[2], "RZ") == 0)
            return set_resize(vol_id, argv[3]);

        return cmderr_syntax(argv[2]);
    }

    if (check_fmt(argc, argv, set_vol_fmt) && name[0] == '\0')
    {
        if (strcasecmp(argv[2], "MT") == 0)
            return set_mount(vol_id);

        if (strcasecmp(argv[2], "UM") == 0)
            return set_umount(vol_id, ctx->vol_id);

        return set_vol_attr(vol_id, argv[2]);
    }

    if (check_fmt(argc, argv, set_file_attr_fmt) && name[0] != '\0')
        return set_file_attr((FsContext){vol_id, ua}, argv[1], argv[2]);

    return cmderr_syntax(NULL);
}

int main(int argc, char **argv)
{
    return ccp_run_app(cmd_set, argc, argv);
}
