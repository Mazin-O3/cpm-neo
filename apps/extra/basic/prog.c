/*
 * apps/extra/basic/prog.c — Program storage and state
 *
 * Tokenized program management (add/delete/find/list/load), the RUN
 * driver, and variable/function-space initialization.  Program entries
 * carry their line number as a little-endian u16 headed by tokenized
 * text, so reads/writes go through the SDK byte-order helpers.
 */

#include "basic.h"

#include <byteorder.h>
#include <string.h>

/* Program line management */

char *prog_find_line(BasicState *s, int n)
{
    char *p = s->prog.data;

    while (p < s->prog.free_ptr)
    {
        int num = get_le16((const uint8_t *)p);

        if (num == n)
            return p;

        p = entry_next(p);
    }

    return NULL;
}

void prog_del_line(BasicState *s, int n)
{
    char *p = prog_find_line(s, n);

    if (!p)
        return;

    char *next = entry_next(p);

    int rest = (int)(s->prog.free_ptr - next);

    memmove(p, next, rest);

    s->prog.free_ptr -= (int)(next - p);
}

/* Insert or replace line n.  An empty text deletes it.  Returns 0 on
 * success, -1 if the program is full (the old line is then left intact). */

int prog_add_line(BasicState *s, int n, const char *t)
{
    if (!*t)
    {
        prog_del_line(s, n);
        return 0;
    }

    char tokened[BASIC_LINE_LEN];

    tokenize_line(tokened, sizeof(tokened), t);

    int   len = 2 + (int)strlen(tokened) + 1;
    char *old = prog_find_line(s, n);
    int   old_len = old ? (int)(entry_next(old) - old) : 0;
    int   used = (int)(s->prog.free_ptr - s->prog.data);

    /* Check the space *before* deleting so a too-long replacement cannot
     * destroy the line it was meant to replace. */
    if (used - old_len + len > BASIC_PROG_MAX)
    {
        printf("\n?PROGRAM FULL\n");
        return -1;
    }

    if (old)
        prog_del_line(s, n);

    char *ins = s->prog.data;

    while (ins < s->prog.free_ptr && get_le16((const uint8_t *)ins) < n)
        ins = entry_next(ins);

    int rest = (int)(s->prog.free_ptr - ins);

    memmove(ins + len, ins, rest);

    put_le16((uint8_t *)ins, (uint16_t)n);
    memcpy(ins + 2, tokened, (size_t)len - 2);

    s->prog.free_ptr += len;

    return 0;
}

/*
 * Handle one line of "NNN text" (typed at the prompt or read from a file).
 * Returns 0 if the line has no leading line number (caller decides what to
 * do with it), 1 if it was stored/deleted, -1 on error (already reported).
 */

int prog_enter_line(BasicState *s, char *line)
{
    char *p = line;
    long  num = 0;
    int   digits = 0;

    while (*p == ' ')
        p++;

    while (*p >= '0' && *p <= '9')
    {
        if (num <= BASIC_MAX_LINE)
            num = num * 10 + (*p - '0');
        p++;
        digits++;
    }

    if (!digits)
        return 0;

    if (num > BASIC_MAX_LINE)
    {
        printf("?BAD LINE NUMBER\n");
        return -1;
    }

    while (*p == ' ')
        p++;

    /* Strip trailing blanks / CR so CRLF files don't smuggle \r into lines. */
    int len = (int)strlen(p);

    while (len > 0 && (unsigned char)p[len - 1] <= ' ')
        p[--len] = 0;

    return prog_add_line(s, (int)num, p) < 0 ? -1 : 1;
}

/* Program listing */

void prog_list(BasicState *s)
{
    int rows = 0;

    char *p = s->prog.data;

    while (p < s->prog.free_ptr)
    {
        int num = get_le16((const uint8_t *)p);

        printf("%d ", num);

        const char *text = p + 2;

        while (*text)
        {
            if ((unsigned char)*text >= BASIC_TOKEN_BASE)
            {
                printf("%s", lexer_kw_name((unsigned char)*text - BASIC_TOKEN_BASE));
                text++;
            }
            else
                putchar(*text++);
        }

        printf("\n");

        if (anykey("...", &rows, CONSOLE_HEIGHT))
        {
            printf("\n");
            break;
        }

        p = entry_next(p);
    }
}

/* Program initialization and control */

static void clear_vars_and_fns(BasicState *s)
{
    s->loop.stack_ptr = -1;
    s->gosub.stack_ptr = -1;

    memset(s->var.val, 0, sizeof(s->var.val));
    memset(s->var.str, 0, sizeof(s->var.str));
    memset(s->var.dim, 0, sizeof(s->var.dim));
    memset(s->fn.param_var_idx, -1, sizeof(s->fn.param_var_idx));
    memset(s->fn.body, 0, sizeof(s->fn.body));

    s->fn.depth = 0;
    s->loop.resume = 0;
    s->ctrl.jump = 0;
}

void prog_new(BasicState *s)
{
    clear_vars_and_fns(s);
    s->prog.free_ptr = s->prog.data;
    s->ctrl.instr_ptr = NULL;
    s->ctrl.stopped = 0;
    s->ctrl.lineno = 0;
}

void clr_vars(BasicState *s)
{
    clear_vars_and_fns(s);
}

/*
 * Execute program lines starting at ctrl.instr_ptr until the program ends,
 * stops, or errors.  A line either falls through to the next one or, when a
 * statement set ctrl.jump, continues at whatever instr_ptr (and, for
 * FOR/NEXT and RETURN, loop.resume) it left behind.
 */

static void run_loop(BasicState *s)
{
    while (s->ctrl.instr_ptr && s->ctrl.instr_ptr < s->prog.free_ptr && !s->ctrl.stopped)
    {
        char *cur = s->ctrl.instr_ptr;

        s->ctrl.lineno = get_le16((const uint8_t *)cur);

        exec_line(s, cur + 2);

        if (!s->ctrl.stopped && !s->ctrl.jump)
            s->ctrl.instr_ptr = entry_next(cur);
    }

    s->ctrl.lineno = 0;
    s->ctrl.jump = 0;
    s->loop.resume = 0;
    s->ctrl.instr_ptr = NULL;
}

void prog_run(BasicState *s)
{
    if (s->prog.free_ptr == s->prog.data)
    {
        printf("\n?NO PROGRAM\n");
        return;
    }

    /* RUN clears variables but keeps DEF FN definitions (classic BASIC). */

    memset(s->var.val, 0, sizeof(s->var.val));
    memset(s->var.str, 0, sizeof(s->var.str));
    memset(s->var.dim, 0, sizeof(s->var.dim));

    s->loop.stack_ptr = -1;
    s->gosub.stack_ptr = -1;
    s->fn.depth = 0;
    s->ctrl.instr_ptr = s->prog.data;
    s->ctrl.stopped = 0;
    s->ctrl.jump = 0;
    s->loop.resume = 0;

    run_loop(s);
}

/* Direct-mode GOTO/GOSUB: keep running the program from the line the
 * direct statement jumped to, without clearing anything. */

void prog_continue(BasicState *s)
{
    run_loop(s);
}

/* Program loading
 *
 * Lines go through prog_enter_line(), so out-of-order and duplicate line
 * numbers behave exactly as if typed.  The current program is only wiped
 * once the file has actually opened. */

int prog_load(BasicState *s, const char *path)
{
    int fd = open(path, "r");

    if (fd < 0)
    {
        printf("?FILE NOT FOUND\n");
        return -1;
    }

    prog_new(s);

    char line[BASIC_LINE_LEN];

    while (readline(fd, line, sizeof(line)) > 0)
    {
        if (prog_enter_line(s, line) < 0)
            break;
    }

    close(fd);

    return 0;
}
