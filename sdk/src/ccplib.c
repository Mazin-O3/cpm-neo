/*
 * sdk/src/ccplib.c
 * Shared command library: argument parsing, filespec helpers, and the
 * command error path.  Compiled into the CCP and into libc.a so every
 * transient command (and any custom .com) gets them for free.
 */

#include "ccplib.h"

/* Shared: index of the volume-colon in a volume ref (arg[1..4]), or -1. */
static int find_vol_colon(const char *arg)
{
    for (int i = 1; i <= 4 && arg[i]; i++)

        if (arg[i] == ':')
            return i;
    return -1;
}

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

    if (isalpha((unsigned char)arg[0]))
    {
        int cp = find_vol_colon(arg);

        if (cp > 0)
        {
            out->fs_ctx.vol_id = toupper((unsigned char)arg[0]) - 'A';

            if (out->fs_ctx.vol_id >= MAX_VOLUMES)
                return 0;

            if (cp > 1)
            {
                char *ep;
                int   ua = strtoi(arg + 1, &ep, 10);

                if ((ep == arg + cp) && ua >= 0 && ua <= USER_AREA_MAX)
                    out->fs_ctx.user_area = (uint8_t)ua;
                else
                    return 0;
            }

            name_copy(out->name, arg + cp + 1, FILENAME_MAX - 1);

            return 1;
        }
    }

    if (isdigit((unsigned char)arg[0]))
    {
        char *ep;
        int   ua = strtoi(arg, &ep, 10);

        if (ep > arg && *ep == ':')
        {
            if (ua >= 0 && ua <= USER_AREA_MAX)
            {
                out->fs_ctx.user_area = (uint8_t)ua;
                name_copy(out->name, ep + 1, FILENAME_MAX - 1);
                return 1;
            }

            return 0;
        }
        /* If no colon is found, it's a normal filename starting with a digit,
           so we intentionally fall through to copy_name below. */
    }

    name_copy(out->name, arg, FILENAME_MAX - 1);

    return 1;
}

/* Validate argument shape against a format string.
 * Tokens (space-separated, keywords case-insensitive):
 *   v, v*      = bare volume ref: V: (no user digit, no filename)
 *   p, p*      = general path: V:, VU:, U:, or bare filename
 *   f, f*      = file ref, VU: prefix optional (FOO.TXT, V:FOO.TXT, U:FOO.TXT)
 *   a          = RO RW SYS DIR MT UM RZ
 *   n          = integer
 *   any other  = exact match (case-insensitive)
 * Suffix * = wildcards (?|*) allowed.
 * Returns 1 on match, 0 on mismatch (no error printed).
 */

typedef enum
{
    TOK_LIT,  /* Exact match against lit (case-insensitive) */
    TOK_VOL,  /* Bare volume: V: */
    TOK_PATH, /* General path: V:, VU:, U:, or bare filename */
    TOK_FILE, /* File ref, VU: prefix optional */
    TOK_ATTR, /* RO RW SYS DIR MT UM RZ */
    TOK_NUM,  /* Integer */
    MAX_FMT_TOKS,
} TokKind;

typedef struct
{
    TokKind     kind;
    const char *lit;        /* Only used for TOK_LIT */
    int         allow_wild; /* Only meaningful for TOK_VOL/TOK_PATH/TOK_FILE */
} FTok;

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

static TokKind kw_lookup(const char *t, int len)
{
    if (len != 1)
        return TOK_LIT;

    switch (toupper(t[0]))
    {
    case 'V':
        return TOK_VOL;
    case 'F':
        return TOK_FILE;
    case 'P':
        return TOK_PATH;
    case 'A':
        return TOK_ATTR;
    case 'N':
        return TOK_NUM;
    default:
        return TOK_LIT;
    }
}

static FTok make_tok(const char *t)
{
    FTok f = {TOK_LIT, t, 0};
    int  len = strlen(t);

    if (len > 0 && t[len - 1] == '*')
    {
        f.allow_wild = 1;
        len--;
    }

    if (len > 0 && len <= 4)
        f.kind = kw_lookup(t, len);

    return f;
}

static int tok_match(const char *arg, const FTok *f)
{
    switch (f->kind)
    {
    case TOK_LIT:
        return strcasecmp(arg, f->lit) == 0;

    case TOK_VOL:
    {
        if (!f->allow_wild && has_wildcard(arg))
            return 0;

        return isalpha((unsigned char)arg[0]) && arg[1] == ':' && arg[2] == '\0';
    }

    case TOK_PATH:
    {
        if (!f->allow_wild && has_wildcard(arg))
            return 0;

        if (*arg == '\0')
            return 0;

        return 1;
    }

    case TOK_FILE:
    {
        if (!f->allow_wild && has_wildcard(arg))
            return 0;

        int         plen = vu_prefix_len(arg);
        const char *filename = arg + plen;

        return *filename != '\0';
    }

    case TOK_ATTR:
    {
        static const char *const attrs[] = {"RO", "RW", "SYS", "DIR", "MT", "UM", "RZ"};

        for (int i = 0; i < 7; i++)

            if (strcasecmp(arg, attrs[i]) == 0)
                return 1;

        return 0;
    }

    case TOK_NUM:
    {
        char *ep;
        strtoi(arg, &ep, 10);
        return (ep > arg) && (*ep == '\0');
    }

    default:
        return 0;
    }
}

int check_fmt(int argc, char **argv, const char *fmt)
{
    char  buf[48];
    char *tok[MAX_FMT_TOKS];
    int   nt = 0;

    strncpy(buf, fmt, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p = buf;

    while (*p && nt < MAX_FMT_TOKS)
    {
        while (*p == ' ')
            p++;

        if (!*p)
            break;

        tok[nt++] = p;

        while (*p && *p != ' ')
            p++;

        if (*p)
            *p++ = '\0';
    }

    if (nt == 0)
        return argc == 1;

    if (argc != nt + 1)
        return 0;

    for (int i = 0; i < nt; i++)
    {
        FTok f = make_tok(tok[i]);

        if (!tok_match(argv[i + 1], &f))
            return 0;
    }

    return 1;
}

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

int parse_int(const char *s, int *out)
{
    char *ep;
    int   v = strtoi(s, &ep, 10);

    if (ep == s || *ep != '\0')
        return 0;
    *out = v;
    return 1;
}

void pad_field(char *out, const char *src, int len, int w)
{
    if (len > w)
        len = w;

    memcpy(out, src, (size_t)len);
    memset(out + len, ' ', (size_t)(w - len));
    out[w] = '\0';
}

void make_batch_path(char *out, int8_t vol)
{
    out[0] = (char)('A' + vol);
    out[1] = '0';
    out[2] = ':';
    strcpy(out + 3, BATCH_NAME);
}

Pager pager_start(void)
{
    Pager p;
    sys_consize(&p.cols, &p.rows);
    p.line_count = 0;
    return p;
}

int pager_line(Pager *p)
{
    return anykey("...", &p->line_count, p->rows);
}

int8_t vol_from_arg(const char *arg, int8_t def)
{
    if (arg[0] && arg[1] == ':' && isalpha((unsigned char)arg[0]))
        return toupper((unsigned char)arg[0]) - 'A';

    return def;
}

void cmderr_print(CmdErr err)
{
    if (err.err_code == CMDERR_SYNTAX)
    {
        if (err.token)
            printf("%s?\n", err.token);
        else
            printf("?\n");
    }
    else
    {
        /* ENOENT/EEXIST are plain errno errors even when a volume is
         * attached; any other volume-scoped failure is a BDOS error. */

        if (err.vol_id >= 0 && err.err_code != ENOENT && err.err_code != EEXIST)
            printf("Bdos Err On %c: %s\n", 'A' + err.vol_id, strerror(err.err_code));
        else
            printf("%s\n", strerror(err.err_code));
    }

    sys_setenv(ENV_RETURN_CODE, (uint32_t)err.err_code);
}

const CmdEntry *cmd_lookup(const CmdEntry *table, const char *name)
{
    for (int i = 0; table[i].name; i++)
    {
        if (!strcasecmp(table[i].name, name))
            return &table[i];
    }

    return NULL;
}

int ccp_run_app(cmd_fn_t fn, int argc, char **argv)
{
    FsContext ctx;
    sys_getctx(&ctx);

    CmdErr err = fn(&ctx, argc, argv);

    if (err.err_code != 0)
        cmderr_print(err);

    return err.err_code;
}
