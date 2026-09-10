#include "basic.h"

static BasicState s;

static void load_line(BasicState *s, const char *p)
{
    while (*p == ' ')
        p++;

    if (*p < '0' || *p > '9')
        return;

    int num = 0;

    while (*p >= '0' && *p <= '9')
        num = num * 10 + (*p++ - '0');

    while (*p == ' ')
        p++;

    int src_len = strlen(p);

    if (s->prog.free + 2 + src_len + 1 > s->prog.data + MAX_TEXT)
        return;

    tokenize_line(s->prog.free + 2, src_len + 1, p);

    int entry_len = 2 + strlen(s->prog.free + 2) + 1;

    s->prog.free[0] = num & 0xFF;

    s->prog.free[1] = (num >> 8) & 0xFF;

    s->prog.free += entry_len;
}

int prog_load(BasicState *s, const char *path)
{
    int fd = open(path, "r");

    if (fd < 0)
    {
        printf("?FILE NOT FOUND\n");
        return -1;
    }

    char line[BASIC_LINE_SZ];

    while (readline(fd, line, sizeof(line)) > 0)
        load_line(s, line);

    close(fd);

    return 0;
}

static int do_load(BasicState *s, char *path)
{
    prog_new(s);
    return prog_load(s, path);
}

static int parse_input_line(BasicState *s, const char *p)
{
    int num = 0, n = 0;

    while (isdigit(p[n]))
        num = num * 10 + (p[n++] - '0');

    if (n == 0)
        return 0;

    while (p[n] == ' ')
        n++;

    prog_add_line(s, num, p[n] ? p + n : "");

    return 1;
}

static int get_filename_arg(BasicState *s, char **out)
{
    if (!lex_next(s))
        return 0;

    if (s->lex.type != T_STR)
    {
        printf("?FILENAME REQUIRED\n");
        return 0;
    }

    const char *p = s->lex.buf;

    while (*p == ' ')
        p++;

    if (!*p)
    {
        printf("?FILENAME REQUIRED\n");
        return 0;
    }

    const char *dot = strrchr(s->lex.buf, '.');
    const char *ext = (dot && dot[1] != '\0') ? dot + 1 : NULL;

    if (!ext || strcasecmp(ext, "bas"))
    {
        printf("?BAD FILE TYPE\n");
        return 0;
    }

    *out = s->lex.buf;

    return 1;
}

static int exec_direct(BasicState *s)
{
    if (s->lex.type != T_KEY)
        return 0;

    switch (s->lex.kw)
    {
    case K_LIST:
        prog_list(s);
        return 1;
    case K_LOAD:
    {
        char *path;

        if (!get_filename_arg(s, &path))
            return 1;

        do_load(s, path);

        return 1;
    }

    case K_RUN:
        prog_run(s);
        return 1;
    case K_FRE:
        printf("  %d BYTES FREE\n\n", MAX_TEXT - (int)(s->prog.free - s->prog.data));
        return 1;
    case K_NEW:
        prog_new(s);
        return 1;
    case K_CLR:
        clr_vars(s);
        return 1;
    case K_SAVE:
    {
        if (s->prog.free == s->prog.data)
        {
            printf("?NO PROGRAM\n");
            return 1;
        }

        char *path;

        if (!get_filename_arg(s, &path))
            return 1;

        int fd = open(path, "w");

        if (fd < 0)
        {
            printf("?%s\n", (fd == EFILERO) ? "File R/O" : "VOL R/O");
            return 1;
        }

        char *p = s->prog.data;

        while (p < s->prog.free)
        {
            char line_buf[256];
            int  pos = 0;
            int  num = (unsigned char)p[0] | ((unsigned char)p[1] << 8);
            pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%d ", num);
            p += 2;

            while (*p && pos < (int)sizeof(line_buf) - 2)
            {
                if ((unsigned char)*p >= TOK_BASE)
                {
                    const char *kw = lex_kw_names[(unsigned char)*p - TOK_BASE];
                    int         klen = strlen(kw);

                    if (pos + klen + 1 < (int)sizeof(line_buf))
                    {
                        memcpy(line_buf + pos, kw, klen);
                        pos += klen;
                        line_buf[pos++] = ' ';
                    }

                    p++;
                }
                else
                    line_buf[pos++] = *p++;
            }

            while (*p)
                p++;
            p++;
            line_buf[pos++] = '\n';
            write(fd, line_buf, pos);
        }

        close(fd);

        return 1;
    }

    case K_EXIT:
        return -1;
    default:
        return 0;
    }
}

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        printf("Use: BASIC <FILENAME.BAS>\n");
        return 1;
    }

    if (argc > 1)
    {
        if (do_load(&s, argv[1]) == 0)
            prog_run(&s);

        return 0;
    }

    prog_new(&s);

    printf("%d Bytes free\n\n", MAX_TEXT);

    char buf[BASIC_LINE_SZ];

    for (;;)
    {
        printf(">");

        if (getline(buf, BASIC_LINE_SZ) < 0)
        {
            putchar('\n');
            return 0;
        }

        char *p = buf;

        while (*p == ' ')
            p++;

        int len = strlen(p);

        while (len > 0 && p[len - 1] == ' ')
            p[--len] = 0;

        if (len == 0)
            continue;

        s.ctl.stopped = 0;

        s.ctl.lineno = 0;

        if (parse_input_line(&s, p))
            continue;

        s.lex.p = p;

        if (!lex_next(&s))
            continue;

        int r = exec_direct(&s);

        if (r < 0)
        {
            return 0;
        }

        if (r > 0)
            continue;

        if (s.ctl.stopped)
            continue;

        s.lex.p = p;

        s.ctl.ip = NULL;

        s.ctl.lineno = 0;

        exec_line(&s, p);
    }

    return 0;
}
