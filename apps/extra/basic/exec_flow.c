/*
 * apps/extra/basic/exec_flow.c — Statement handlers
 *
 * Individual statement executors (PRINT, INPUT, GOTO/GOSUB/RETURN,
 * FOR/NEXT, POKE, DIM, DEF) dispatched from exec_stmt() in exec_core.c.
 */

#include "exec.h"

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

void exec_input(BasicState *s)
{
    if (!lexer_next(s))
        return;

    int prompt_shown = 0;

    if (s->lex.type == T_STR)
    {
        printf("%s", s->lex.buf);
        prompt_shown = 1;
        lexer_next(s);

        if (s->lex.type == T_SYM && s->lex.buf[0] == ';')
            lexer_next(s);
    }

    if (s->lex.type != T_VAR)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int vn = s->lex.num, is_str = s->lex.is_string;

    lexer_next(s);

    int idx = -1;

    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
    {
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

    if (prompt_shown)
        printf("\n");

    printf("? ");

    char ibuf[BASIC_STR_LEN];

    getline(ibuf, BASIC_STR_LEN);

    if (is_str)
        strcpy(s->var.str[vn], ibuf);
    else if (idx >= 0)
        s->var.arr[vn][idx] = atoi(ibuf);
    else
        s->var.val[vn] = atoi(ibuf);
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
        ctrl_error(s, "UNDEFINED LINE");
    else
        s->ctrl.instr_ptr = idx;
}

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

    s->gosub.stk[++s->gosub.stack_ptr] = entry_next(s->ctrl.instr_ptr);
    s->ctrl.instr_ptr = idx;
}

void exec_return(BasicState *s)
{
    if (s->gosub.stack_ptr < 0)
    {
        ctrl_error(s, "RETURN WITHOUT GOSUB");
        return;
    }

    s->ctrl.instr_ptr = s->gosub.stk[s->gosub.stack_ptr--];
}

void exec_for(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_VAR)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int vn = s->lex.num;

    lexer_next(s);

    if (!lex_expect_sym(s, '='))
        return;

    s->var.val[vn] = expr_eval(s);

    if (s->ctrl.stopped)
        return;

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

void exec_next(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_VAR)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int vn = s->lex.num;

    if (s->loop.stack_ptr < 0 || s->loop.var_idx[s->loop.stack_ptr] != vn)
    {
        ctrl_error(s, "NEXT WITHOUT FOR");
        return;
    }

    if (ctrl_break_key(s))
        return;

    s->var.val[vn] += s->loop.step[s->loop.stack_ptr];

    if ((s->loop.step[s->loop.stack_ptr] > 0 && s->var.val[vn] > s->loop.tgt[s->loop.stack_ptr]) ||
        (s->loop.step[s->loop.stack_ptr] < 0 && s->var.val[vn] < s->loop.tgt[s->loop.stack_ptr]))
    {
        s->loop.stack_ptr--;
        lexer_next(s);
    }
    else
    {
        if (!s->loop.ret_instr_ptr[s->loop.stack_ptr])
        {
            s->lex.ptr = s->loop.ret_src[s->loop.stack_ptr];
            lexer_next(s);
        }
        else
        {
            s->loop.resume = s->loop.ret_src[s->loop.stack_ptr];
            s->ctrl.instr_ptr = s->loop.ret_instr_ptr[s->loop.stack_ptr];
        }
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

    *((volatile uint8_t *)a) = (uint8_t)v;
}

void exec_dim(BasicState *s)
{
    if (!lexer_next(s))
        return;

    if (s->lex.type != T_VAR || s->lex.is_string)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    int vn = s->lex.num;

    lexer_next(s);

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
}

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

    lexer_next(s);

    if (!lex_expect_sym(s, '('))
        return;

    if (s->lex.type != T_VAR)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    s->fn.param_var_idx[fn_idx] = s->lex.num;

    lexer_next(s);

    if (!lex_expect_sym(s, ')'))
        return;

    if (!lex_chk_sym(s, '='))
        return;

    /* Snapshot the body text: the source may be a direct-mode stack buffer
     * or tokenized program text that later memmoves, so the pointer cannot
     * be kept verbatim. */
    if (strlen(s->lex.ptr) >= sizeof(s->fn.text[0]))
    {
        ctrl_error(s, "FUNCTION BODY TOO LONG");
        return;
    }

    strcpy(s->fn.text[fn_idx], s->lex.ptr);
    s->fn.body[fn_idx] = s->fn.text[fn_idx];

    lexer_skip_line(s);
}