/*
 * apps/extra/basic/exec_core.c — Statement dispatch
 *
 * The statement dispatcher (exec_stmt), conditional execution (exec_if),
 * and variable assignment.  The individual statement handlers live in
 * exec_flow.c and program storage in prog.c.
 */

#include "exec.h"

#include <string.h>

/* Variable assignment */

static void assign_variable(BasicState *s, int vn, int is_str)
{
    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
    {
        /* Array element */
        lexer_next(s);
        int idx = expr_eval(s);

        if (s->ctrl.stopped)
            return;

        if (!lex_expect_sym(s, ')'))
            return;

        if (!lex_expect_sym(s, '='))
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

        if (is_str)
        {
            if (s->lex.type == T_STR)
            {
                strncpy(s->var.str[vn], s->lex.buf, BASIC_STR_LEN - 1);
                s->var.str[vn][BASIC_STR_LEN - 1] = '\0';
                lexer_next(s);
            }
            else
                ctrl_error(s, "SYNTAX ERROR");
        }
        else
            s->var.arr[vn][idx] = expr_eval(s);
    }
    else
    {
        /* Simple variable */
        if (!lex_expect_sym(s, '='))
            return;

        if (is_str)
        {
            if (s->lex.type == T_STR)
            {
                strncpy(s->var.str[vn], s->lex.buf, BASIC_STR_LEN - 1);
                s->var.str[vn][BASIC_STR_LEN - 1] = '\0';
                lexer_next(s);
            }
            else if (s->lex.type == T_VAR && s->lex.is_string)
            {
                strncpy(s->var.str[vn], s->var.str[s->lex.num], BASIC_STR_LEN - 1);
                s->var.str[vn][BASIC_STR_LEN - 1] = '\0';
                lexer_next(s);
            }
            else
                ctrl_error(s, "SYNTAX ERROR");
        }
        else
            s->var.val[vn] = expr_eval(s);
    }
}

/* Statement handlers */

static void exec_if(BasicState *s)
{
    if (!lexer_next(s))
        return;

    int cond;

    /* String comparison — special path, not part of the expression grammar */
    if (s->lex.type == T_STR || (s->lex.type == T_VAR && s->lex.is_string))
    {
        char s1[BASIC_STR_LEN], s2[BASIC_STR_LEN];
        int  n1 = -1, n2 = -1;

        if (s->lex.type == T_STR)
            strncpy(s1, s->lex.buf, BASIC_STR_LEN - 1);
        else
        {
            n1 = s->lex.num;
            strncpy(s1, s->var.str[n1], BASIC_STR_LEN - 1);
        }
        s1[BASIC_STR_LEN - 1] = '\0';
        lexer_next(s);

        if (s->lex.type != T_SYM ||
            (s->lex.buf[0] != '=' && s->lex.buf[0] != '<' && s->lex.buf[0] != '>'))
        {
            ctrl_error(s, "SYNTAX ERROR");
            return;
        }

        char op0 = s->lex.buf[0], op1 = s->lex.buf[1];

        lexer_next(s);

        if (s->lex.type == T_STR)
            strncpy(s2, s->lex.buf, BASIC_STR_LEN - 1);
        else if (s->lex.type == T_VAR && s->lex.is_string)
        {
            n2 = s->lex.num;
            strncpy(s2, s->var.str[n2], BASIC_STR_LEN - 1);
        }
        else
        {
            ctrl_error(s, "SYNTAX ERROR");
            return;
        }
        s2[BASIC_STR_LEN - 1] = '\0';
        lexer_next(s);

        int cmp = strcmp(s1, s2);

        if (op0 == '=' && !op1)
            cond = cmp == 0;
        else if (op0 == '<' && !op1)
            cond = cmp < 0;
        else if (op0 == '>' && !op1)
            cond = cmp > 0;
        else if (op0 == '<' && op1 == '=')
            cond = cmp <= 0;
        else if (op0 == '>' && op1 == '=')
            cond = cmp >= 0;
        else if (op0 == '<' && op1 == '>')
            cond = cmp != 0;
        else
        {
            ctrl_error(s, "SYNTAX ERROR");
            return;
        }
    }
    else
        cond = expr_eval(s);

    if (s->ctrl.stopped)
        return;

    if (!lex_expect_key(s, K_THEN))
        return;

    if (cond)
    {
        if (s->lex.type == T_NUM)
        {
            char *idx = prog_find_line(s, s->lex.num);

            if (!idx)
                ctrl_error(s, "UNDEFINED LINE");
            else
                s->ctrl.instr_ptr = idx;
        }
        else
            exec_stmt(s);
    }
    else
        lexer_skip_line(s);
}

/* Statement dispatch */

void exec_stmt(BasicState *s)
{
    if (s->lex.type == T_EOF)
        return;

    if (s->lex.type == T_VAR)
    {
        int vn = s->lex.num;
        int is_str = s->lex.is_string;

        lexer_next(s);

        if ((s->lex.type == T_SYM && s->lex.buf[0] == '(') ||
            (s->lex.type == T_SYM && s->lex.buf[0] == '='))
            assign_variable(s, vn, is_str);
        else
            ctrl_error(s, "SYNTAX ERROR");
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
        lexer_next(s);

        if (s->lex.type != T_VAR)
        {
            ctrl_error(s, "SYNTAX ERROR");
            break;
        }

        assign_variable(s, s->lex.num, s->lex.is_string);
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