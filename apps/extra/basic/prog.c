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

void prog_add_line(BasicState *s, int n, const char *t)
{
    prog_del_line(s, n);

    if (!*t)
        return;

    char tokened[512];

    tokenize_line(tokened, sizeof(tokened), t);

    int len = 2 + (int)strlen(tokened) + 1;

    char *prev = s->prog.data;
    char *ins = s->prog.data;

    while (ins < s->prog.free_ptr)
    {
        int num = get_le16((const uint8_t *)ins);

        if (num > n)
            break;

        prev = entry_next(ins);
        ins = prev;
    }

    if (s->prog.free_ptr + len > s->prog.data + BASIC_PROG_MAX)
    {
        printf("\n?PROGRAM FULL\n");
        return;
    }

    int rest = (int)(s->prog.free_ptr - ins);

    memmove(ins + len, ins, rest);

    put_le16((uint8_t *)ins, (uint16_t)n);
    memcpy(ins + 2, tokened, (size_t)len - 2);

    s->prog.free_ptr += len;
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

    s->loop.resume = 0;
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
    s->ctrl.instr_ptr = s->prog.data;
    s->ctrl.stopped = 0;
    s->loop.resume = 0;

    while (s->ctrl.instr_ptr && s->ctrl.instr_ptr < s->prog.free_ptr && !s->ctrl.stopped)
    {
        char *cur = s->ctrl.instr_ptr;

        s->ctrl.lineno = get_le16((const uint8_t *)cur);

        exec_line(s, cur + 2);

        if (!s->ctrl.stopped)
        {
            if (s->ctrl.instr_ptr == cur && !s->loop.resume)
                s->ctrl.instr_ptr = entry_next(cur);
        }
    }

    s->ctrl.lineno = 0;
}

/* Program loading */

int prog_load(BasicState *s, const char *path)
{
    int fd = open(path, "r");

    if (fd < 0)
    {
        printf("?FILE NOT FOUND\n");
        return -1;
    }

    char line[BASIC_LINE_LEN];

    while (readline(fd, line, sizeof(line)) > 0)
    {
        char *p = line;

        while (*p == ' ')
            p++;

        if (*p < '0' || *p > '9')
            continue;

        int num = atoi(p);

        while (*p >= '0' && *p <= '9')
            p++;

        while (*p == ' ')
            p++;

        int src_len = (int)strlen(p);

        if (s->prog.free_ptr + 2 + src_len + 1 > s->prog.data + BASIC_PROG_MAX)
            continue;

        tokenize_line(s->prog.free_ptr + 2, (unsigned)src_len + 1, p);

        int entry_len = 2 + (int)strlen(s->prog.free_ptr + 2) + 1;

        put_le16((uint8_t *)s->prog.free_ptr, (uint16_t)num);

        s->prog.free_ptr += entry_len;
    }

    close(fd);

    return 0;
}