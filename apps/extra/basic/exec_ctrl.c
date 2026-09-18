/*
 * apps/extra/basic/exec_ctrl.c — Execution loop and interrupts
 *
 * Error reporting, break-key polling, and the per-line execution loop
 * that feeds statements to the dispatcher in exec_core.c.
 */

#include "basic.h"

/* Error handling and control */

void ctrl_error(BasicState *s, const char *msg)
{
    if (s->ctrl.lineno)
        printf("\n?%s IN LINE %d\n", msg, s->ctrl.lineno);
    else
        printf("\n?%s\n", msg);
    s->ctrl.stopped = 1;
}

int ctrl_break_key(BasicState *s)
{
    if (!peekchar())
        return 0;

    int c = getchar();

    if (c == CH_ESC || c == CH_BREAK)
    {
        s->ctrl.stopped = 1;
        return 1;
    }

    return 0;
}

/* Line execution */

void exec_line(BasicState *s, const char *t)
{
    if (ctrl_break_key(s))
    {
        printf("\n");
        return;
    }

    char *saved_ci = s->ctrl.instr_ptr;

    if (s->loop.resume)
    {
        s->lex.ptr = s->loop.resume;
        s->loop.resume = 0;
    }
    else
        s->lex.ptr = (char *)t;

    if (!lexer_next(s))
        return;

    while (s->lex.type != T_EOF && !s->ctrl.stopped)
    {
        if (s->lex.type == T_SYM && s->lex.buf[0] == ':')
        {
            lexer_next(s);
            continue;
        }

        exec_stmt(s);

        if (s->ctrl.instr_ptr != saved_ci || s->loop.resume)
        {
            lexer_skip_line(s);
            s->lex.type = T_EOF;
        }
        else if (!*s->lex.ptr)
            s->lex.type = T_EOF;
    }
}