#include "basic.h"

/* Expression error handling */

static int expr_err(BasicState *s)
{
    ctrl_error(s, "SYNTAX ERROR");
    return 0;
}

static int expr_chk_sym(BasicState *s, char ch)
{
    if (s->lex.type != T_SYM || s->lex.buf[0] != ch)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    return 1;
}

/* Primary expression parsing */

static int expr_parse_primary(BasicState *s)
{
    if (s->lex.type == T_NUM)
    {
        int v = s->lex.num;

        lexer_next(s);
        return v;
    }

    if (s->lex.type == T_VAR)
    {
        int var_idx = s->lex.num;
        int is_string_var = s->lex.is_string;
        lexer_next(s);

        if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
        {
            if (is_string_var)
                return expr_err(s);

            int arr_idx = expr_parse_paren(s);

            if (s->ctrl.stopped)
                return 0;

            int out_val;

            if (var_aget(s, var_idx, arr_idx, &out_val) < 0)
                return 0;

            return out_val;
        }

        if (is_string_var)
            return expr_err(s);

        return s->var.val[var_idx];
    }

    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')

        return expr_parse_paren(s);

    if (s->lex.type == T_KEY)
    {
        int fn_kw = s->lex.kw;
        lexer_next(s);
        int arg = expr_parse_paren(s);

        if (s->ctrl.stopped)
            return 0;

        switch (fn_kw)
        {
        case K_PEEK:
            return (*(volatile uint8_t *)arg);
        case K_ABS:
            return arg < 0 ? -arg : arg;
        case K_SGN:
            return arg < 0 ? -1 : arg > 0 ? 1 : 0;
        case K_RND:
            return arg > 0 ? (rand() % arg) + 1 : 0;
        default:
            exec_syntax_err(s);
            return 0;
        }
    }

    if (s->lex.type == T_FN)
    {
        int fn_idx = s->lex.num;

        lexer_next(s);
        int arg = expr_parse_paren(s);

        if (s->ctrl.stopped)
            return 0;

        if (s->fn.param_var_idx[fn_idx] < 0)
        {
            ctrl_error(s, "UNDEFINED FUNCTION");
            return 0;
        }

        BasicLex saved_lex = s->lex;
        int      param_idx = s->fn.param_var_idx[fn_idx];
        int      old_val = s->var.val[param_idx];
        s->var.val[param_idx] = arg;
        s->lex.ptr = s->fn.body[fn_idx];
        lexer_next(s);
        int result = expr_eval(s);
        s->var.val[param_idx] = old_val;
        s->lex = saved_lex; /* Restore lexer state safely */
        return result;
    }

    return expr_err(s);
}

/* Logical and math handlers omitted for brevity but they follow
   the exact same expr_parse_* naming conventions... */

/* Expression evaluation entry points */

int expr_eval(BasicState *s)
{
    return expr_parse_primary(s); /* Simplified for demonstration */
}

int expr_parse_paren(BasicState *s)
{
    lexer_next(s);
    int val = expr_eval(s);

    if (s->ctrl.stopped)
        return 0;

    if (!expr_chk_sym(s, ')'))
        return 0;

    lexer_next(s);
    return val;
}
