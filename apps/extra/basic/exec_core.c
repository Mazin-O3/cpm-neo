/*
 * apps/extra/basic/exec_core.c — Statement dispatch
 *
 * The statement dispatcher (exec_stmt), conditional execution (exec_if),
 * and variable assignment.  The individual statement handlers live in
 * exec_flow.c and program storage in prog.c.
 */

#include "basic.h"

#include <string.h>

/* Variable assignment */

static void assign_string(BasicState *s, int vn)
{
    if (s->lex.type == T_STR)
    {
        strncpy(s->var.str[NAME_LETTER(vn)], s->lex.buf, BASIC_STR_LEN - 1);
        s->var.str[NAME_LETTER(vn)][BASIC_STR_LEN - 1] = '\0';
        lexer_next(s);
    }
    else if (s->lex.type == T_VAR && s->lex.is_string)
    {
        if (s->lex.num != vn)
            memcpy(s->var.str[NAME_LETTER(vn)], s->var.str[NAME_LETTER(s->lex.num)], BASIC_STR_LEN);
        lexer_next(s);
    }
    else
        ctrl_error(s, "SYNTAX ERROR");
}

/* Current token is the one just after the variable name: '(' or '=' */

static void assign_variable(BasicState *s, int vn, int is_str)
{
    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
    {
        /* Array element (numeric only — there are no string arrays) */
        if (is_str)
        {
            ctrl_error(s, "SYNTAX ERROR");
            return;
        }

        int *ref = expr_array_ref(s, vn);

        if (!ref)
            return;

        if (!lex_expect_sym(s, '='))
            return;

        int v = expr_eval(s);

        if (!s->ctrl.stopped)
            *ref = v;

        return;
    }

    /* Simple variable */
    if (!lex_expect_sym(s, '='))
        return;

    if (is_str)
        assign_string(s, vn);
    else
    {
        int v = expr_eval(s);

        /* Never store the placeholder 0 that a failed expression returns. */
        if (!s->ctrl.stopped)
            s->var.val[vn] = v;
    }
}

/* Current token is a T_VAR: [LET] V = ...  or  V(i) = ... */

static void exec_assign(BasicState *s)
{
    int vn = s->lex.num;
    int is_str = s->lex.is_string;

    if (!lexer_next(s))
        return;

    if (s->lex.type == T_SYM && (s->lex.buf[0] == '(' || s->lex.buf[0] == '='))
        assign_variable(s, vn, is_str);
    else
        ctrl_error(s, "SYNTAX ERROR");
}

/* Statement handlers */

static void exec_if(BasicState *s)
{
    if (!lexer_next(s))
        return;

    /* String comparisons are ordinary primaries now, so this one path
     * covers  IF A$="X" AND B>1 THEN ...  as well as numeric tests. */
    int cond = expr_eval(s);

    if (s->ctrl.stopped)
        return;

    if (!lex_expect_key(s, K_THEN))
        return;

    if (!cond)
    {
        lexer_skip_line(s);
        return;
    }

    if (s->lex.type == T_NUM)
    {
        char *idx = prog_find_line(s, s->lex.num);

        if (!idx)
            ctrl_error(s, "UNDEFINED LINE");
        else
        {
            s->ctrl.instr_ptr = idx;
            s->ctrl.jump = 1;
        }
    }
    else
        exec_stmt(s);
}

/* Statement dispatch */

void exec_stmt(BasicState *s)
{
    if (s->lex.type == T_EOF)
        return;

    if (s->lex.type == T_VAR)
    {
        exec_assign(s);
        return;
    }

    if (s->lex.type != T_KEY)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return;
    }

    switch (s->lex.kw)
    {
    case K_LET:
        if (!lexer_next(s))
            break;

        if (s->lex.type != T_VAR)
        {
            ctrl_error(s, "SYNTAX ERROR");
            break;
        }

        exec_assign(s);
        break;

    case K_REM:
        lexer_skip_line(s);
        break;

    case K_POKE:
        exec_poke(s);
        break;

    case K_PRINT:
        exec_print(s);
        break;

    case K_INPUT:
        exec_input(s);
        break;

    case K_GOTO:
        exec_goto(s);
        break;

    case K_GOSUB:
        exec_gosub(s);
        break;

    case K_RETURN:
        exec_return(s);
        break;

    case K_IF:
        exec_if(s);
        break;

    case K_FOR:
        exec_for(s);
        break;

    case K_NEXT:
        exec_next(s);
        break;

    case K_END:
        s->ctrl.stopped = 1;
        break;

    case K_DIM:
        exec_dim(s);
        break;

    case K_DEF:
        exec_def(s);
        break;

    default:
        ctrl_error(s, "SYNTAX ERROR");
        break;
    }
}
