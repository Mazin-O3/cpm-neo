/*
 * apps/extra/basic/exec_flow.c — Statement handlers
 *
 * Individual statement executors (PRINT, INPUT, GOTO/GOSUB/RETURN,
 * FOR/NEXT, POKE, DIM, DEF) dispatched from exec_stmt() in exec_core.c.
 */

#include "basic.h"

#include <string.h>

void exec_print(BasicState *s)
{
    if (!lexer_next(s))
        return;

    int no_nl = 0;

    for (;;)
    {
        if (s->lex.type == T_VAR && s->lex.is_string)
        {
            printf("%s", s->var.str[s->lex.num]);
            lexer_next(s);
        }
        else if (s->lex.type == T_STR)
        {
            printf("%s", s->lex.buf);
            lexer_next(s);
        }
        else if (s->lex.type == T_SYM && s->lex.buf[0] == ':')
            break;
        else if (s->lex.type != T_EOF)
        {
            int v = expr_eval(s);

            if (s->ctrl.stopped)
                return;

            printf("%d", v);
        }
        else
            break;

        if (s->lex.type == T_SYM && s->lex.buf[0] == ';')
        {
            no_nl = 1;
            lexer_next(s);
        }
        else if (s->lex.type == T_SYM && s->lex.buf[0] == ',')
        {
            no_nl = 1;
            printf("\t");
            lexer_next(s);
        }
        else
            no_nl = 0;
    }

    if (!no_nl && !s->ctrl.stopped)
        putchar('\n');
}

/* INPUT ["prompt";] V [, V ...]   (V may be A, A$ or A(i)) */

void exec_input(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type == T_STR)
    {
        printf("%s", s->lex.buf);

        if (!lexer_next(s))
            return;

        if (s->lex.type == T_SYM && (s->lex.buf[0] == ';' || s->lex.buf[0] == ','))
        {
            if (!lexer_next(s))
                return;
        }
    }

    for (;;)
    {
        if (s->lex.type != T_VAR)
        {
            ctrl_error(s, "SYNTAX ERROR");
            return;
        }

        int vn = s->lex.num, is_str = s->lex.is_string;

        if (!lexer_next(s))
            return;

        int idx = -1;

        if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
        {
            if (is_str)
            {
                ctrl_error(s, "SYNTAX ERROR");
                return;
            }

            lexer_next(s);
            idx = expr_eval(s);

            if (s->ctrl.stopped)
                return;

            if (!lex_expect_sym(s, ')'))
                return;

            if (s->var.dim[vn] == 0)
            {
                ctrl_error(s, "UNDIMENSIONED ARRAY");
                return;
            }

            if (idx < 0 || idx >= s->var.dim[vn])
            {
                ctrl_error(s, "SUBSCRIPT OUT OF RANGE");
                return;
            }
        }

        char ibuf[BASIC_STR_LEN];

        ibuf[0] = '\0';
        printf("? ");

        /* End of input (EOF / closed console) ends the program instead of
         * feeding uninitialised bytes to atoi()/strcpy(). */
        if (getline(ibuf, BASIC_STR_LEN) < 0)
        {
            s->ctrl.stopped = 1;
            return;
        }

        ibuf[BASIC_STR_LEN - 1] = '\0';

        if (is_str)
            strcpy(s->var.str[vn], ibuf);
        else if (idx >= 0)
            s->var.arr[vn][idx] = atoi(ibuf);
        else
            s->var.val[vn] = atoi(ibuf);

        if (s->lex.type == T_SYM && s->lex.buf[0] == ',')
        {
            if (!lexer_next(s))
                return;
            continue;
        }

        break;
    }
}

void exec_goto(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_NUM)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    char *idx = prog_find_line(s, s->lex.num);

    if (!idx)
    {
        ctrl_error(s, "UNDEFINED LINE");
        return;
    }

    s->ctrl.instr_ptr = idx;
    s->ctrl.jump = 1;
}

/*
 * GOSUB remembers both the line and the position inside it, so RETURN
 * resumes with the statements that follow the GOSUB on the same line.
 * From direct mode instr_ptr is NULL: the NULL is stored as a marker and
 * the matching RETURN simply ends the run.
 */

void exec_gosub(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_NUM)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    char *idx = prog_find_line(s, s->lex.num);

    if (!idx)
    {
        ctrl_error(s, "UNDEFINED LINE");
        return;
    }

    if (s->gosub.stack_ptr >= BASIC_GOSUB_DEPTH - 1)
    {
        ctrl_error(s, "GOSUB OVERFLOW");
        return;
    }

    s->gosub.stack_ptr++;
    s->gosub.stk[s->gosub.stack_ptr] = s->ctrl.instr_ptr;
    s->gosub.src[s->gosub.stack_ptr] = s->lex.ptr;

    s->ctrl.instr_ptr = idx;
    s->ctrl.jump = 1;
}

void exec_return(BasicState *s)
{
    if (s->gosub.stack_ptr < 0)
    {
        ctrl_error(s, "RETURN WITHOUT GOSUB");
        return;
    }

    int   sp = s->gosub.stack_ptr--;
    char *line = s->gosub.stk[sp];

    if (!line)
    {
        /* Subroutine was entered from direct mode: nothing to go back to. */
        s->ctrl.stopped = 1;
        return;
    }

    s->ctrl.instr_ptr = line;
    s->loop.resume = s->gosub.src[sp];
    s->ctrl.jump = 1;
}

void exec_for(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_VAR || s->lex.is_string)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int vn = s->lex.num;

    if (!lexer_next(s))
        return;

    if (!lex_expect_sym(s, '='))
        return;

    int start = expr_eval(s);

    if (s->ctrl.stopped)
        return;

    s->var.val[vn] = start;

    if (!lex_expect_key(s, K_TO))
        return;

    int end = expr_eval(s);

    if (s->ctrl.stopped)
        return;

    int step = 1;

    if (s->lex.type == T_KEY && s->lex.kw == K_STEP)
    {
        lexer_next(s);
        step = expr_eval(s);

        if (s->ctrl.stopped)
            return;
    }

    /* Re-entering a FOR whose previous run was left via GOTO must replace
     * the stale frame (and anything nested inside it), not stack on top. */
    for (int i = s->loop.stack_ptr; i >= 0; i--)
    {
        if (s->loop.var_idx[i] == vn)
        {
            s->loop.stack_ptr = i - 1;
            break;
        }
    }

    if (s->loop.stack_ptr >= BASIC_FOR_DEPTH - 1)
    {
        ctrl_error(s, "FOR OVERFLOW");
        return;
    }

    s->loop.stack_ptr++;
    s->loop.var_idx[s->loop.stack_ptr] = vn;
    s->loop.tgt[s->loop.stack_ptr] = end;
    s->loop.step[s->loop.stack_ptr] = step;
    s->loop.ret_instr_ptr[s->loop.stack_ptr] = s->ctrl.instr_ptr;
    s->loop.ret_src[s->loop.stack_ptr] = s->lex.ptr;
}

/* NEXT [V] — bare NEXT closes the innermost loop; NEXT V also discards any
 * loops nested inside V's. */

void exec_next(BasicState *s)
{
    if (!lexer_next(s))
        return;

    int vn;
    int named = 0;

    if (s->lex.type == T_VAR && !s->lex.is_string)
    {
        vn = s->lex.num;
        named = 1;
    }
    else if (s->lex.type == T_EOF || (s->lex.type == T_SYM && s->lex.buf[0] == ':'))
    {
        if (s->loop.stack_ptr < 0)
        {
            ctrl_error(s, "NEXT WITHOUT FOR");
            return;
        }

        vn = s->loop.var_idx[s->loop.stack_ptr];
    }
    else
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int f = s->loop.stack_ptr;

    while (f >= 0 && s->loop.var_idx[f] != vn)
        f--;

    if (f < 0)
    {
        ctrl_error(s, "NEXT WITHOUT FOR");
        return;
    }

    s->loop.stack_ptr = f;

    if (ctrl_break_key(s))
        return;

    int v = s->var.val[vn];
    int step = s->loop.step[f];
    int tgt = s->loop.tgt[f];
    int done;

    /* Stop instead of wrapping when v + step would overflow: otherwise
     * FOR I=1 TO 32767 (INT_MAX) can never terminate. */
    if ((step > 0 && v > INT_MAX - step) || (step < 0 && v < (-INT_MAX - 1) - step))
        done = 1;
    else
    {
        v += step;
        done = (step > 0 && v > tgt) || (step < 0 && v < tgt);
    }

    s->var.val[vn] = v;

    if (done)
    {
        s->loop.stack_ptr--;

        if (named)
            lexer_next(s);

        return;
    }

    if (!s->loop.ret_instr_ptr[f])
    {
        /* Direct mode: the body is later in the same input buffer. */
        s->lex.ptr = s->loop.ret_src[f];
        lexer_next(s);
    }
    else
    {
        s->loop.resume = s->loop.ret_src[f];
        s->ctrl.instr_ptr = s->loop.ret_instr_ptr[f];
        s->ctrl.jump = 1;
    }
}

void exec_poke(BasicState *s)
{
    if (!lexer_next(s))
        return;

    int a = expr_eval(s);

    if (s->ctrl.stopped)
        return;

    if (!lex_expect_sym(s, ','))
        return;

    int v = expr_eval(s);

    if (s->ctrl.stopped)
        return;

    *BASIC_ADDR(a) = (uint8_t)v;
}

/* DIM A(n) [, B(n) ...] */

void exec_dim(BasicState *s)
{
    if (!lexer_next(s))
        return;

    for (;;)
    {
        if (s->lex.type != T_VAR || s->lex.is_string)
        {
            ctrl_error(s, "SYNTAX ERROR");
            return;
        }

        int vn = s->lex.num;

        if (!lexer_next(s))
            return;

        if (!lex_expect_sym(s, '('))
            return;

        int size = expr_eval(s);

        if (s->ctrl.stopped)
            return;

        if (!lex_expect_sym(s, ')'))
            return;

        if (size < 1 || size > BASIC_MAX_DIM)
        {
            ctrl_error(s, "BAD DIMENSION");
            return;
        }

        s->var.dim[vn] = size + 1;

        for (int i = 0; i <= size; i++)
            s->var.arr[vn][i] = 0;

        if (s->lex.type == T_SYM && s->lex.buf[0] == ',')
        {
            if (!lexer_next(s))
                return;
            continue;
        }

        break;
    }
}

/*
 * DEF FNx(p) = expr
 *
 * The body ends at the first ':' outside a string, so
 *     DEF FNA(X)=X*X : PRINT FNA(3)
 * works.  The text is copied because the source may be a direct-mode stack
 * buffer or tokenized program text that later memmoves.  The parameter and
 * body are published together and only after every check passed, so a
 * failed DEF can never leave a half-defined function behind.
 */

void exec_def(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_FN)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int fn_idx = s->lex.num;

    if (!lexer_next(s))
        return;

    if (!lex_expect_sym(s, '('))
        return;

    if (s->lex.type != T_VAR || s->lex.is_string)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int param = s->lex.num;

    if (!lexer_next(s))
        return;

    if (!lex_expect_sym(s, ')'))
        return;

    if (!lex_chk_sym(s, '='))
        return;

    const char *body = s->lex.ptr;

    while (*body && (unsigned char)*body <= ' ')
        body++;

    const char *end = body;
    int         in_str = 0;

    while (*end)
    {
        if (*end == '"')
            in_str = !in_str;
        else if (*end == ':' && !in_str)
            break;
        end++;
    }

    size_t len = (size_t)(end - body);

    if (len == 0)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    if (len >= sizeof(s->fn.text[0]))
    {
        ctrl_error(s, "FUNCTION BODY TOO LONG");
        return;
    }

    memcpy(s->fn.text[fn_idx], body, len);
    s->fn.text[fn_idx][len] = '\0';
    s->fn.body[fn_idx] = s->fn.text[fn_idx];
    s->fn.param_var_idx[fn_idx] = param;

    /* Continue after the body: lands on ':' or end of line. */
    s->lex.ptr = (char *)end;
    lexer_next(s);
}
