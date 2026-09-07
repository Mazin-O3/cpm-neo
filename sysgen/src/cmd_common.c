/*
 * sysgen/src/cmd_common.c — Shared CLI parsing, target context & helpers
 *
 * Option/argument parsers, the --dst/--attr/--disk target context, and the
 * low-level "add one host file to the disk image" primitive.  These are the
 * pieces shared by the file-ops commands (cmd_files.c), the bundled-app
 * installer (cmd_apps.c) and system generation (cmd_new.c).
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

/* ========================================================================= *
 * Shared Path/Name Helpers
 * ========================================================================= */

/* Handles both POSIX and Windows path separators; returns a pointer
 * into the original string (no allocation). */
const char *extract_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = path;

    if (slash && slash >= base)
        base = slash + 1;

    if (backslash && backslash >= base)
        base = backslash + 1;

    return base;
}

static void n83_dot(const char *n83, char *out, size_t n)
{
    int nb = NAME83_BASE;
    int ne = NAME83_EXT;
    int o = 0;

    while (nb > 0 && n83[nb - 1] == ' ')
        nb--;

    while (ne > 0 && n83[NAME83_BASE + ne - 1] == ' ')
        ne--;

    if (nb > 0 && nb < (int)n)
    {
        memcpy(out, n83, (size_t)nb);
        o = nb;
    }

    if (ne > 0 && o + 1 + ne < (int)n)
    {
        out[o++] = '.';
        memcpy(out + o, n83 + NAME83_BASE, (size_t)ne);
        o += ne;
    }

    out[o] = '\0';
}

/* ========================================================================= *
 * CLI Option Parsers & Target Context
 * ========================================================================= */

int parse_dst(int argc, char **argv, int *vol, int *user)
{
    int seen = 0;
    const char *v = flag_value(argc, argv, "--dst", &seen);

    if (!seen || !v || !*v)
    {
        *vol = VOL_A;
        *user = 0;
        return 0;
    }

    if (parse_vn(v, vol, user) != 0)
    {
        err("invalid --dst '%s' (expected Vn, e.g. A0, B7)", v);
        return 1;
    }

    return 0;
}

/*
 * Parse --attr (RO, RW, SYS, or combinations).  When the flag is
 * omitted, |attr| is set to |dflt| instead of defaulting to RW.
 * Returns 0 on success.
 */
int parse_attr_dflt(int argc, char **argv, uint8_t *attr, uint8_t dflt)
{
    int seen = 0;
    const char *v = flag_value(argc, argv, "--attr", &seen);
    *attr = 0;

    if (!seen || !v || !*v)
    {
        *attr = dflt;
        return 0;
    }

    char buf[32];
    strncpy(buf, v, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char *tok = strtok(buf, "+,"); tok; tok = strtok(NULL, "+,"))
    {
        if (strcasecmp(tok, "RW") == 0 || strcasecmp(tok, "R/W") == 0)
            continue;

        if (strcasecmp(tok, "RO") == 0 || strcasecmp(tok, "R/O") == 0)
            *attr |= FILE_ATTR_READ_ONLY;
        else if (strcasecmp(tok, "SYS") == 0)
            *attr |= FILE_ATTR_SYSTEM;
        else
        {
            err("invalid --attr '%s' (expected RO, RW, SYS, or a SYS+RO combination)", v);
            return 1;
        }
    }

    return 0;
}

int setup_disk_target(int argc, char **argv, const char *vn_arg, ImageTarget *tgt)
{
    const char *disk = resolve_disk(argc, argv, tgt->disk_path, sizeof(tgt->disk_path));

    int vol = VOL_A, user = 0;

    if (vn_arg && parse_vn(vn_arg, &vol, &user) != 0)
    {
        err("invalid volume '%s'", vn_arg);
        return 1;
    }

    if (open_disk(disk) != 0 || disk_init() != 0 || mount_vol((int8_t)vol) != 0)
        return 1;

    tgt->ctx.vol_id = (int8_t)vol;
    tgt->ctx.user_area = (uint8_t)user;
    return 0;
}

const char *get_str_flag(int argc, char **argv, const char *flag_name)
{
    int is_present = 0;
    const char *value = flag_value(argc, argv, flag_name, &is_present);
    return (is_present && value && *value != '\0') ? value : NULL;
}

bool get_bool_flag(int argc, char **argv, const char *flag_name)
{
    int is_present = 0;
    flag_value(argc, argv, flag_name, &is_present);
    return is_present != 0;
}

int check_flags(int argc, char **argv, const char *const *allowed)
{
    return reject_unknown_flags(argc, argv, allowed);
}

int check_positionals(int argc, char **argv, int min_pos, int max_pos)
{
    const char *pos[8];
    int n = collect_positional(argc, argv, pos, 8);

    if (n < min_pos || n > max_pos)
    {
        err("unexpected arguments");
        return 1;
    }

    return 0;
}

/* ========================================================================= *
 * Disk & File Write Primitive
 * ========================================================================= */

/* Add a single host file to the currently mounted volume/user area.
 * Returns 0 on success, 1 on error, 2 on skip (already exists). */
int add_data_open(const char *file, const AddFileOpts *opts)
{
    uint8_t *data = NULL;
    uint32_t len = 0;

    if (read_file(file, &data, &len) != 0)
    {
        err("cannot read '%s'", file);
        return 1;
    }

    const char *base = extract_basename(file);
    char n83[NAME83_LEN + 1];
    to_name83(base, n83);
    n83[NAME83_LEN] = '\0';

    FsContext ctx = {(int8_t)opts->vol, (uint8_t)opts->user};

    char dot[NAME83_LEN + 2];
    n83_dot(n83, dot, sizeof(dot));

    /* Skip files that already exist on the destination volume/user area. */
    FileInfo fi;

    if (bd_find(n83, ctx, &fi, 0) > 0)
    {
        free(data);
        printf("  %s %-13s -> %c:%u  (already exists, skipped)\n", opts->verb, dot, 'A' + opts->vol,
               opts->user);
        return 2;
    }

    int fd = bd_create(n83, ctx);

    if (fd < 0)
    {
        free(data);
        err("create %s (%s)", dot, err_str(fd));
        return 1;
    }

    uint32_t off = 0;
    int rc = EOK;

    while (off < len)
    {
        uint16_t chunk = ((len - off) > 1024) ? 1024 : (uint16_t)(len - off);
        rc = bd_write(fd, data + off, chunk);

        if (rc < 0)
            break;

        off += (uint32_t)rc;
    }

    bd_close(fd);

    if (rc >= 0 && opts->attr != 0)
        rc = bd_fsetattr(n83, ctx, opts->attr);

    free(data);

    if (rc < 0)
    {
        err("write %s (%s)", dot, err_str(rc));
        return 1;
    }

    const char *tag = (opts->attr & FILE_ATTR_SYSTEM)      ? "  [SYS]"
                      : (opts->attr & FILE_ATTR_READ_ONLY) ? "  [RO]"
                                                           : "";
    char h[24];
    hr(h, sizeof(h), len);
    printf("  %s %-13s %9s  -> %c:%u%s\n", opts->verb, dot, h, 'A' + opts->vol, opts->user, tag);
    return 0;
}