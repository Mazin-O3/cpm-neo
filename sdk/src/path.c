/*
 * sdk/src/path.c — Filespec prefix and 8.3 name parsing/building
 *
 * The one implementation of the filespec vocabulary declared in path.h.
 * Compiled into the kernel, the CCP, and libc.a so every layer agrees on
 * a single rule for "X:", "Xn:" and "n:" filespecs.  Depends only on
 * string.h, ctype.h and fsctx.h, so it links into the kernel without the
 * rest of the CCP command library.
 */

#include "path.h"

#include <ctype.h>
#include <string.h>
/* Filespec parsing */

/* Copy up to n chars of src into out, always NUL-terminated. */
void name_copy(char *out, const char *src, size_t n)
{
    strncpy(out, src, n);
    out[n] = '\0';
}

int has_wildcard(const char *arg)
{
    return strchr(arg, '*') || strchr(arg, '?');
}

int parse_fileref(FsContext *ctx, const char *arg, FileRef *out)
{
    out->fs_ctx = *ctx;

    const char *rest = split_prefix(arg, &out->fs_ctx);

    if (rest != arg)
        name_copy(out->name, rest, FILENAME_MAX - 1);
    else
        name_copy(out->name, arg, FILENAME_MAX - 1);

    return 1;
}

/* Length of the VU: prefix (including colon), or 0 if none.
   Accepts V:, VU: (1-2 user digits), and U: (user-only).
   Requires at least one char before the colon. */
int vu_prefix_len(const char *arg)
{
    int i = 0;

    if (isalpha((unsigned char)arg[0]))
        i++;

    if (isdigit((unsigned char)arg[i]))
        i++;

    if (isdigit((unsigned char)arg[i]))
        i++;

    return (i > 0 && arg[i] == ':') ? (i + 1) : 0;
}

/* Consume a run of digit characters from p (at most max; max < 0 = any
 * run).  Returns the number of digits consumed, writing their value to
 * *ua — or 0 when the run starts with a non-digit / -1 when the value
 * exceeds USER_AREA_MAX.  The value is checked after every digit, so it
 * can never overflow.  The caller validates the run length it needs. */
static const char *parse_ua_run(const char *p, int max, int *ua)
{
    const char *start = p;
    int         count = 0;
    int         value = 0;

    while ((max < 0 || count < max) && isdigit((unsigned char)p[count]))
    {
        value = value * 10 + p[count] - '0';

        if (value > USER_AREA_MAX)
            return start;

        count++;
    }

    if (count == 0)
        return start;

    *ua = value;

    return p + count;
}

/* "X:" / "Xn:" — a volume letter with an optional bounded user area. */
static const char *parse_alpha_prefix(const char *p, FsContext *ctx)
{
    int colon = -1;

    for (int i = 1; i <= 4 && p[i]; i++)
        if (p[i] == ':')
        {
            colon = i;
            break;
        }

    if (colon < 0)
        return p;

    int vol = toupper((unsigned char)p[0]) - 'A';

    if (vol >= MAX_VOLUMES)
        return p;

    if (colon > 1)
    {
        int ua;

        if (parse_ua_run(p + 1, colon - 1, &ua) != p + 1 + (colon - 1))
            return p;

        ctx->user_area = (uint8_t)ua;
    }

    ctx->vol_id = (int8_t)vol;

    return p + colon + 1;
}

/* "n:" — a user area on its own (stays on the current volume). */
static const char *parse_digit_prefix(const char *p, FsContext *ctx)
{
    int         ua;
    const char *end = parse_ua_run(p, -1, &ua);

    if (end == p || *end != ':')
        return p;

    ctx->user_area = (uint8_t)ua;

    return end + 1;
}

const char *split_prefix(const char *p, FsContext *ctx)
{
    if (isalpha((unsigned char)p[0]))
        return parse_alpha_prefix(p, ctx);

    if (isdigit((unsigned char)p[0]))
        return parse_digit_prefix(p, ctx);

    return p;
}

/* Path and name building */

char *make_path(char *buf, FsContext ctx, const char *name)
{
    int i = 0;
    buf[i++] = 'A' + ctx.vol_id;

    if (ctx.user_area >= 10)
        buf[i++] = '0' + ctx.user_area / 10;

    buf[i++] = '0' + ctx.user_area % 10;
    buf[i++] = ':';

    name_copy(buf + i, name, FILENAME_MAX - 1);

    return buf;
}

SplitName split_name83(const char *name)
{
    SplitName   sn;
    const char *dot = strchr(name, '.');

    sn.base = name;
    sn.base_len = dot ? (int)(dot - name) : (int)strlen(name);

    if (sn.base_len > NAME83_BASE)
        sn.base_len = NAME83_BASE;

    if (dot && dot[1])
    {
        sn.ext = dot + 1;
        sn.ext_len = (int)strlen(dot + 1);
    }
    else
    {
        sn.ext = "";
        sn.ext_len = 0;
    }

    if (sn.ext_len > NAME83_EXT) /* Keep "%.*s" within 8.3 field width */
        sn.ext_len = NAME83_EXT;

    return sn;
}

void make_batch_path(char *out, int8_t vol)
{
    out[0] = (char)('A' + vol);
    out[1] = '0';
    out[2] = ':';
    strcpy(out + 3, BATCH_NAME);
}

int8_t vol_from_arg(const char *arg, int8_t def)
{
    if (arg[0] && arg[1] == ':' && isalpha((unsigned char)arg[0]))
        return toupper((unsigned char)arg[0]) - 'A';

    return def;
}