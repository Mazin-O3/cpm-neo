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

/*
 * Program entry layout — the ONLY place that knows it.
 *
 *     [ u16 line number ][ u8 text length ][ tokenized text ][ NUL ]
 *
 * The length byte lets a search hop from entry to entry with a pointer add
 * instead of running strlen() over every byte it skips: line lookups (GOTO,
 * GOSUB, IF..THEN n, program editing) walk entries, not bytes.  Tokenized
 * text is never longer than its source line, so it always fits in a byte.
 *
 * Everything else goes through entry_line(), entry_text(), entry_next() and
 * entry_write(), so the layout can change without touching any caller.
 */

#define ENTRY_HDR 3

int entry_line(const char *p)
{
    return get_le16((const uint8_t *)p);
}

char *entry_text(char *p)
{
    return p + ENTRY_HDR;
}

char *entry_next(char *p)
{
    return p + ENTRY_HDR + (unsigned char)p[2] + 1;
}

/* Store line n with already-tokenized text tok at dst. */

static void entry_write(char *dst, int n, const char *tok)
{
    size_t len = strlen(tok);

    put_le16((uint8_t *)dst, (uint16_t)n);
    dst[2] = (char)len;
    memcpy(dst + ENTRY_HDR, tok, len + 1);
}

/* Program line management */

char *prog_find_line(BasicState *s, int n)
{
    char *p = s->prog.data;

    while (p < s->prog.free_ptr)
    {
        int num = entry_line(p);

        if (num == n)
            return p;

        if (num > n)
            return NULL;            /* sorted: it cannot be further on */

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

/* Tokenize t into tok (BASIC_LINE_LEN bytes) and return the size the stored
 * entry will occupy: 2-byte line number + text + NUL.  Every path that stores
 * a line uses this, so the size accounting cannot drift between them. */

static int entry_size(char *tok, const char *t)
{
    tokenize_line(tok, BASIC_LINE_LEN, t);

    return ENTRY_HDR + (int)strlen(tok) + 1;
}

/* Append line n at the end of the program.  The caller guarantees n is higher
 * than every stored line, so no search is needed — O(1) instead of two full
 * scans.  Same return convention as prog_add_line(). */

static int prog_append(BasicState *s, int n, const char *t)
{
    if (!*t)
        return 0;                       /* nothing stored to delete */

    char tokened[BASIC_LINE_LEN];
    int  len = entry_size(tokened, t);

    if ((int)(s->prog.free_ptr - s->prog.data) + len > BASIC_PROG_MAX)
    {
        printf("\n?PROGRAM FULL\n");
        return -1;
    }

    entry_write(s->prog.free_ptr, n, tokened);

    s->prog.free_ptr += len;

    return 0;
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

    int   len = entry_size(tokened, t);
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

    while (ins < s->prog.free_ptr && entry_line(ins) < n)
        ins = entry_next(ins);

    int rest = (int)(s->prog.free_ptr - ins);

    memmove(ins + len, ins, rest);

    entry_write(ins, n, tokened);

    s->prog.free_ptr += len;

    return 0;
}

/*
 * Split "NNN text" into its line number and the text after it.  Returns the
 * number of digits found (0 = the line has no line number).  *rest points at
 * the text with leading blanks skipped and trailing blanks / CR stripped
 * (in place), so CRLF files don't smuggle \r into program lines.  *num stops
 * growing once it exceeds BASIC_MAX_LINE, so it can never overflow.
 */

static int split_line(char *line, long *num, char **rest)
{
    char *p = line;
    int   digits = 0;

    *num = 0;

    while (*p == ' ')
        p++;

    while (*p >= '0' && *p <= '9')
    {
        if (*num <= BASIC_MAX_LINE)
            *num = *num * 10 + (*p - '0');
        p++;
        digits++;
    }

    while (*p == ' ')
        p++;

    int len = (int)strlen(p);

    while (len > 0 && (unsigned char)p[len - 1] <= ' ')
        p[--len] = 0;

    *rest = p;

    return digits;
}

static int line_number_ok(long num)
{
    if (num > BASIC_MAX_LINE)
    {
        printf("?BAD LINE NUMBER\n");
        return 0;
    }

    return 1;
}

/*
 * Handle one line of "NNN text" (typed at the prompt or read from a file).
 * Returns 0 if the line has no leading line number (caller decides what to
 * do with it), 1 if it was stored/deleted, -1 on error (already reported).
 */

int prog_enter_line(BasicState *s, char *line)
{
    long  num;
    char *p;

    if (!split_line(line, &num, &p))
        return 0;

    if (!line_number_ok(num))
        return -1;

    return prog_add_line(s, (int)num, p) < 0 ? -1 : 1;
}

/* Program listing */

void prog_list(BasicState *s)
{
    int rows = 0;

    char *p = s->prog.data;

    while (p < s->prog.free_ptr)
    {
        int num = entry_line(p);

        printf("%d ", num);

        const char *text = entry_text(p);

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

static void clear_arrays(BasicState *s)
{
    s->var.pool_top = 0;
    memset(s->var.a_rows, 0, sizeof(s->var.a_rows));
    memset(s->var.a_cols, 0, sizeof(s->var.a_cols));
}

static void clear_vars_and_fns(BasicState *s)
{
    s->loop.stack_ptr = -1;
    s->gosub.stack_ptr = -1;

    memset(s->var.val, 0, sizeof(s->var.val));
    memset(s->var.str, 0, sizeof(s->var.str));
    clear_arrays(s);
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

        s->ctrl.lineno = entry_line(cur);

        exec_line(s, entry_text(cur));

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
    clear_arrays(s);

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
 * LOAD is all-or-nothing with respect to what is left in memory: if any line
 * cannot be stored (program full, bad line number) the partial program is
 * discarded and -1 is returned, so a truncated program can never be RUN.
 *
 * Files are almost always in ascending line order, so a line whose number is
 * higher than any seen so far is appended directly (prog_append, no search).
 * Anything else — out of order, a duplicate, a delete — goes through
 * prog_add_line() and behaves exactly as if typed.  `last` only ever
 * over-estimates the highest stored line (a deleted line leaves it high), which
 * merely sends a few lines down the slow path; it can never send an unordered
 * line down the fast one.  The current program is replaced only once the file
 * has actually opened. */

int prog_load(BasicState *s, const char *path)
{
    int fd = open(path, "r");

    if (fd < 0)
    {
        printf("?FILE NOT FOUND\n");
        return -1;
    }

    prog_new(s);

    char line[BASIC_READ_BUF];
    long last = -1;
    int  rc = 0;

    while (readline(fd, line, sizeof(line)) > 0)
    {
        long  num;
        char *p;
        int   r;

        /* Buffer is one byte over the limit, so a full buffer means the line
         * was too long.  Any such line aborts the load — even an unnumbered
         * one, because a cut-off tail could otherwise be misread as a new
         * numbered line. */
        if (strlen(line) > BASIC_SRC_MAX)
        {
            if (split_line(line, &num, &p))
                printf("?LINE %ld TOO LONG\n", num);
            else
                printf("?LINE TOO LONG\n");

            rc = -1;
            break;
        }

        if (!split_line(line, &num, &p))
            continue;

        if (!line_number_ok(num))
        {
            rc = -1;
            break;
        }

        if (num > last)
        {
            r = prog_append(s, (int)num, p);
            last = num;
        }
        else
            r = prog_add_line(s, (int)num, p);

        if (r < 0)
        {
            rc = -1;
            break;
        }
    }

    close(fd);

    if (rc < 0)
        prog_new(s);

    return rc;
}
