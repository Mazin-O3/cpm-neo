/*
 * sdk/src/ccplib.c
 * Shared command library: argument-shape validation (check_fmt), generic
 * parse/format helpers, and the command error path.  Compiled into the
 * CCP and into libc.a so every transient command (and any custom .com)
 * gets them for free.  Filespec/path parsing lives in path.h/path.c.
 */

#include "ccplib.h"

#include <string.h>

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
        int v;

        return parse_int(arg, &v);
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

int parse_int(const char *s, int *out)
{
    const char *p = s;

    while (isspace((unsigned char)*p))
        p++;

    if (*p == '+' || *p == '-')
        p++;

    if (!isdigit((unsigned char)*p))
        return 0;

    for (; *p; p++)
        if (!isdigit((unsigned char)*p))
            return 0;

    *out = atoi(s);

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

Pager pager_start(void)
{
    Pager p = {.cols = CONSOLE_WIDTH, .rows = CONSOLE_HEIGHT, .line_count = 0};
    return p;
}

int pager_line(Pager *p)
{
    return anykey("...", &p->line_count, p->rows);
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
    for (int i = 0; i < CCP_NUM_CMDS; i++)
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