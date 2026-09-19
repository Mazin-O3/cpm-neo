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
    /* Only the first error of a failure is reported; the cascade of
     * follow-on "SYNTAX ERROR"s a bad token can trigger is suppressed. */
    if (s->ctrl.stopped)
        return;

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

/* Line execution
 *
 * Runs one line of text (a stored program line or a direct-mode line).
 * On return, ctrl.jump tells the caller whether a statement transferred
 * control; if so ctrl.instr_ptr is the new line and loop.resume, when set,
 * is where inside that line execution must continue.
 */

void exec_line(BasicState *s, const char *t)
{
    s->ctrl.jump = 0;

    if (ctrl_break_key(s))
    {
        s->loop.resume = 0;
        printf("\n");
        return;
    }

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

        /* A jump abandons whatever is left of this line. */
        if (s->ctrl.jump)
            lexer_skip_line(s);
    }
}
