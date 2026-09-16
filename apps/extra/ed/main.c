#include "ed.h"

static Editor e;

/* Parsed command: an optional address prefix "[-]N[,N]" followed by an
 * uppercased command letter.  rest points at anything after the letter. */
typedef struct
{
    int         has_from;
    int         from;
    int         has_comma;
    int         has_to;
    int         to;
    char        op;
    const char *rest;
} EdCmd;

/* Parse "[-]N[,N]<op><rest>".  The empty-line (Enter) case is handled by
 * the caller before this is called. */
static void parse_cmd(const char *cmd, EdCmd *c)
{
    int i = 0;
    int ch = (unsigned char)cmd[i++];

    c->has_from = 0;
    c->from = 0;
    c->has_comma = 0;
    c->has_to = 0;
    c->to = 0;

    int neg = 0;

    if (ch == '-')
    {
        neg = 1;
        ch = (unsigned char)cmd[i++];
    }

    if (isdigit(ch))
    {
        c->has_from = 1;
        c->from = atoi(cmd + i - 1);

        while (isdigit(ch))
            ch = (unsigned char)cmd[i++];
    }

    if (neg)
        c->from = -c->from;

    if (ch == ',')
    {
        c->has_comma = 1;
        ch = (unsigned char)cmd[i++];

        if (isdigit(ch))
        {
            c->has_to = 1;
            c->to = atoi(cmd + i - 1);

            while (isdigit(ch))
                ch = (unsigned char)cmd[i++];
        }
    }

    c->op = (char)toupper(ch);
    c->rest = cmd + i;
}

/* Prompt */

static void ed_prompt(Editor *e)
{
    if (e->cur < 0)
        printf("     : *");
    else
        printf("    %d: *", e->cur + 1);
}

/* Read/only guard */
static int ed_check_writ(Editor *e)
{
    if (e->readonly)
    {
        printf("** FILE IS READ/ONLY **\n");
        return 1;
    }

    return 0;
}

/* Commands.  Each returns EOK to continue editing, nonzero to exit. */

static int cmd_advance(Editor *e)
{
    int next = e->cur + 1;

    if (next >= e->num_lines)
        printf("END OF FILE\n");
    else
    {
        e->cur = next;
        printf("     %d: %s\n", next + 1, e->buf + log_to_phys(e, e->line_off[next]));
    }

    return EOK;
}

static int cmd_top(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    e->cur = -1;

    return EOK;
}

static int cmd_list(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    int f, t;

    if (c->has_comma)
    {
        f = c->has_from ? c->from - 1 : 0;
        t = c->has_to ? c->to - 1 : e->num_lines - 1;
    }
    else if (c->has_from)
    {
        if (c->from >= 0)
        {
            f = (e->cur < 0) ? 0 : e->cur;
            t = f + c->from - 1;
        }
        else
        {
            int cur = (e->cur < 0) ? 0 : e->cur;
            f = cur + c->from;

            if (f < 0)
                f = 0;
            t = cur - 1;
        }

        if (t >= e->num_lines)
            t = e->num_lines - 1;

        if (f > t)
        {
            printf("?\n");
            return EOK;
        }
    }
    else
    {
        f = (e->cur < 0) ? 0 : e->cur;
        t = f + 9;
    }

    ed_list(e, f, t);

    return EOK;
}

static int cmd_select(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    if (c->has_from && !c->has_comma)
    {
        if (c->from < 1 || c->from > e->num_lines)
            printf("?RANGE\n");
        else
        {
            e->cur = c->from - 1;
            ed_list(e, e->cur, e->cur);
        }
    }
    else
        printf("?\n");

    return EOK;
}

static int cmd_delete(Editor *e, const EdCmd *c)
{
    if (ed_check_writ(e))
        return EOK;

    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    if (c->has_comma)
    {
        int f = c->has_from ? c->from - 1 : 0;
        int t = c->has_to ? c->to - 1 : e->num_lines - 1;
        ed_delete(e, f, t);
    }
    else if (c->has_from)
        ed_delete(e, c->from - 1, c->from - 1);
    else
    {
        if (e->cur < 0 || e->cur >= e->num_lines)
            printf("?RANGE\n");
        else
            ed_delete(e, e->cur, e->cur);
    }

    return EOK;
}

static int cmd_insert(Editor *e, const EdCmd *c)
{
    if (ed_check_writ(e))
        return EOK;

    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    ed_insert(e, c->has_from ? c->from - 1 : e->cur + 1);

    return EOK;
}

static int cmd_subst(Editor *e, const EdCmd *c)
{
    if (ed_check_writ(e))
        return EOK;

    const char *q = c->rest;
    int         sep = (unsigned char)*q++;

    if (sep != '/')
    {
        printf("?\n");
        return EOK;
    }

    char        old[64], new_s[64];
    const char *p = strchr(q, sep);

    if (!p || p - q >= (int)sizeof(old))
    {
        printf("?\n");
        return EOK;
    }

    int len = (int)(p - q);
    memcpy(old, q, len);
    old[len] = 0;
    q = p + 1;
    p = strchr(q, sep);

    if (!p || p - q >= (int)sizeof(new_s))
    {
        printf("?\n");
        return EOK;
    }

    len = (int)(p - q);
    memcpy(new_s, q, len);
    new_s[len] = 0;
    q = p + 1;

    if (*q)
    {
        printf("?\n");
        return EOK;
    }

    ed_subst(e, c->has_from ? c->from - 1 : e->cur, old, new_s);

    return EOK;
}

static int cmd_read(Editor *e, const EdCmd *c)
{
    if (ed_check_writ(e))
        return EOK;

    char fname[ARG_LEN_MAX];
    strncpy(fname, c->rest, sizeof(fname) - 1);
    fname[sizeof(fname) - 1] = 0;

    if (fname[0])
        ed_read(e, c->has_from ? c->from - 1 : e->cur + 1, fname);

    return EOK;
}

static int cmd_home_save(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    if (ed_save(e) != EOK)
        return EOK;

    e->cur = -1;

    return EOK;
}

static int cmd_exit(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    if (ed_save(e) == EOK)
        return 1;

    return EOK;
}

static int cmd_quit(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    if (e->modified)
    {
        int a = getchar();
        putchar(a);
        putchar('\n');

        if (a != 'Y' && a != 'y')
            return EOK;
    }

    return 1;
}

static int cmd_toggle(Editor *e, const EdCmd *c)
{
    if (*c->rest)
    {
        printf("?\n");
        return EOK;
    }

    e->verify = !e->verify;

    return EOK;
}

/* Entry point */

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: ED <file>\n");
        return 0;
    }

    memset(&e, 0, sizeof(e));
    e.cur = -1;
    e.verify = 1;

    int rc = ed_load(&e, argv[1]);

    if (rc != EOK && rc != ENOENT)
        printf("?%s\n", strerror(rc));

    if (e.num_lines == 0)
        printf("NEW FILE\n");

    for (;;)
    {
        ed_prompt(&e);

        char cmd[80];
        int  cmdlen = getline(cmd, sizeof(cmd));

        if (cmdlen < 0)
            continue;

        if (cmd[0] == '\0')
        {
            cmd_advance(&e);
            continue;
        }

        EdCmd c;
        parse_cmd(cmd, &c);

        int done = EOK;

        switch (c.op)
        {
        case 'B':
            done = cmd_top(&e, &c);
            break;
        case 'L':
            done = cmd_list(&e, &c);
            break;
        case 'D':
            done = cmd_delete(&e, &c);
            break;
        case 'I':
            done = cmd_insert(&e, &c);
            break;
        case 'S':
            done = cmd_subst(&e, &c);
            break;
        case '#':
            done = cmd_toggle(&e, &c);
            break;
        case 'H':
            done = cmd_home_save(&e, &c);
            break;
        case 'R':
            done = cmd_read(&e, &c);
            break;
        case 'E':
            done = cmd_exit(&e, &c);
            break;
        case 'Q':
            done = cmd_quit(&e, &c);
            break;
        default:
            done = cmd_select(&e, &c);
            break;
        }

        if (done != EOK)
            break;
    }

    return 0;
}