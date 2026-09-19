/*
 * apps/extra/basic/main.c — BASIC entry point
 *
 * Owns the interpreter state, the interactive REPL, batch mode
 * (running a .bas file from the command line), and direct-mode
 * commands (LIST, LOAD, SAVE, RUN, NEW, CLR, FRE, EXIT).
 */

#include "basic.h"

#include <byteorder.h>
#include <string.h>

static BasicState g_bs;

/* Direct-mode arguments */

static int get_filename_arg(BasicState *s, char **out)
{
    if (!lexer_next(s))
        return 0;

    if (s->lex.type == T_EOF)
    {
        printf("?FILENAME REQUIRED\n");
        return 0;
    }

    if (s->lex.type != T_STR)
    {
        ctrl_error(s, "SYNTAX ERROR");
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

    const char *dot = strrchr(p, '.');
    const char *ext = (dot && dot[1] != '\0') ? dot + 1 : NULL;

    if (!ext || strcasecmp(ext, "bas"))
    {
        printf("?BAD FILE TYPE\n");
        return 0;
    }

    *out = s->lex.buf;

    return 1;
}

/* Direct-mode commands */

static int exec_direct(BasicState *s)
{
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

        BasicLex saved = s->lex;

        /* A lexer failure (e.g. a stray quote) already reported its error
         * and reads back as T_EOF, so it must be checked separately. */
        if (!lexer_next(s))
            return 1;

        if (s->lex.type != T_EOF)
        {
            ctrl_error(s, "SYNTAX ERROR");

            return 1;
        }

        s->lex = saved;

        prog_load(s, path);

        return 1;
    }
    case K_RUN:
        prog_run(s);
        return 1;

    case K_FRE:
        printf("  %d BYTES FREE\n\n", BASIC_PROG_MAX - (int)(s->prog.free_ptr - s->prog.data));
        return 1;

    case K_NEW:
        prog_new(s);
        return 1;

    case K_CLR:
        clr_vars(s);
        return 1;

    case K_SAVE:
    {
        char *path;

        if (!get_filename_arg(s, &path))
            return 1;

        BasicLex saved = s->lex;

        /* A lexer failure (e.g. a stray quote) already reported its error
         * and reads back as T_EOF, so it must be checked separately. */
        if (!lexer_next(s))
            return 1;

        if (s->lex.type != T_EOF)
        {
            ctrl_error(s, "SYNTAX ERROR");

            return 1;
        }

        s->lex = saved;

        if (s->prog.free_ptr == s->prog.data)
        {
            printf("?NO PROGRAM\n");
            return 1;
        }

        int fd = open(path, "w");

        if (fd < 0)
        {
            printf("?%s\n", (fd == EFILERO) ? "File R/O" : "VOL R/O");
            return 1;
        }

        char *p = s->prog.data;

        while (p < s->prog.free_ptr)
        {
            char line_buf[256];
            int  pos = 0;
            int  num = get_le16((const uint8_t *)p);

            pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%d ", num);
            p += 2;

            while (*p && pos < (int)sizeof(line_buf) - 2)
            {
                if ((unsigned char)*p >= BASIC_TOKEN_BASE)
                {
                    const char *kw = lexer_kw_name((unsigned char)*p - BASIC_TOKEN_BASE);
                    int         klen = (int)strlen(kw);

                    /* No extra space: the source text already carries
                     * whatever followed the keyword, and adding one made
                     * the file grow on every LOAD/SAVE round trip. */
                    if (pos + klen < (int)sizeof(line_buf) - 1)
                    {
                        memcpy(line_buf + pos, kw, (size_t)klen);
                        pos += klen;
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
            write(fd, line_buf, (unsigned)pos);
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

/* True for the commands that take no argument and so must be alone on the
 * line.  (Statements like PRINT/IF/FOR/GOTO obviously must not be checked.) */

static int is_bare_command(int kw)
{
    return kw == K_LIST || kw == K_RUN || kw == K_NEW || kw == K_CLR || kw == K_FRE ||
           kw == K_EXIT;
}

/* Entry point */

int main(int argc, char **argv)
{
    srand(0);

    BasicState *s = &g_bs;

    /* Static state starts zeroed (stack_ptr 0, free_ptr NULL); initialise
     * it properly before anything — including a failed batch-mode load —
     * looks at it. */
    prog_new(s);

    if (argc > 2)
    {
        printf("Use: BASIC <FILENAME.BAS>\n");
        return 1;
    }

    if (argc > 1)
    {
        prog_load(s, argv[1]);

        if (s->prog.free_ptr != s->prog.data)
            prog_run(s);

        return 0;
    }

    printf("*** NeoBasic ***\n%d bytes free\n\n", BASIC_PROG_MAX);

    char buf[BASIC_LINE_LEN];

    for (;;)
    {
        printf(">");

        if (getline(buf, BASIC_LINE_LEN) < 0)
        {
            putchar('\n');
            return 0;
        }

        char *p = buf;

        while (*p == ' ')
            p++;

        int len = (int)strlen(p);

        while (len > 0 && p[len - 1] == ' ')
            p[--len] = 0;

        if (len == 0)
            continue;

        s->ctrl.stopped = 0;
        s->ctrl.lineno = 0;

        /* "NNN text" enters/replaces/deletes a program line. */
        if (prog_enter_line(s, p))
            continue;

        s->lex.ptr = p;

        if (!lexer_next(s))
            continue;

        if (s->lex.type == T_KEY)
        {
            int kw = s->lex.kw;

            /* Bare commands (LIST, RUN, NEW, ...) reject trailing garbage.
             * This used to run for *every* keyword except LOAD/SAVE, which
             * made PRINT/IF/FOR/GOTO/POKE/DIM all fail with SYNTAX ERROR. */
            if (is_bare_command(kw))
            {
                BasicLex saved = s->lex;

                /* Lexer error (stray quote, unknown word...) is already
                 * reported; don't run the command anyway. */
                if (!lexer_next(s))
                    continue;

                if (s->lex.type != T_EOF)
                {
                    ctrl_error(s, "SYNTAX ERROR");

                    continue;
                }

                s->lex = saved;
            }

            int r = exec_direct(s);

            if (r < 0)
                return 0;

            if (r)
                continue;
        }

        if (s->ctrl.stopped)
            continue;

        /* Not a direct-mode command — execute the line immediately.
         * Loop/GOSUB frames never outlive the input line they were made in
         * (their pointers reference `buf`, which the next prompt reuses). */
        s->lex.ptr = p;
        s->ctrl.instr_ptr = NULL;
        s->ctrl.lineno = 0;
        s->ctrl.jump = 0;
        s->loop.resume = 0;
        s->loop.stack_ptr = -1;
        s->gosub.stack_ptr = -1;
        s->fn.depth = 0;

        exec_line(s, p);

        /* Direct-mode GOTO / GOSUB / IF..THEN n: run the program from the
         * target line, keeping variables. */
        if (s->ctrl.jump && s->ctrl.instr_ptr && !s->ctrl.stopped)
        {
            s->loop.stack_ptr = -1;
            prog_continue(s);
        }
    }
}
