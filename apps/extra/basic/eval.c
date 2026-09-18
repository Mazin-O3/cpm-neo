/*
 * apps/extra/basic/eval.c — Expression evaluator
 *
 * Recursive-descent Pratt parser with clean operator precedence.
 * Handles: + - * / \ MOD ^ = < > <= >= AND OR NOT unary -
 * String comparison is not part of this grammar (handled in exec.c).
 */

#include "basic.h"

#include <string.h>

/* Operator precedence — low binds loosest */

static int op_prec(int kw)
{
    switch (kw)
    {
    case K_OR:
        return 1;
    case K_AND:
        return 2;
    case '=':
    case '<':
    case '>':
        return 3;
    case '+':
    case '-':
        return 4;
    case '*':
    case '/':
    case '\\':
    case K_MOD:
        return 5;
    case '^':
        return 6;
    default:
        return -1;
    }
}

/* Forward declaration */
static int expr(BasicState *s, int minprec);

/* Parse a primary expression: number, variable, array, (expr), function */

static int primary(BasicState *s)
{
    /* Number literal */
    if (s->lex.type == T_NUM)
    {
        int v = s->lex.num;
        lexer_next(s);
        return v;
    }

    /* Variable or array element */
    if (s->lex.type == T_VAR)
    {
        int vn = s->lex.num;
        int is_str = s->lex.is_string;

        lexer_next(s);

        /* Array subscript */
        if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
        {
            if (is_str)
            {
                ctrl_error(s, "SYNTAX ERROR");
                return 0;
            }

            lexer_next(s);
            int idx = expr(s, 1);

            if (s->ctrl.stopped)
                return 0;

            if (s->lex.type != T_SYM || s->lex.buf[0] != ')')
            {
                ctrl_error(s, "SYNTAX ERROR");
                return 0;
            }

            lexer_next(s);

            if (s->var.dim[vn] == 0)
            {
                ctrl_error(s, "UNDIMENSIONED ARRAY");
                return 0;
            }

            if (idx < 0 || idx >= s->var.dim[vn])
            {
                ctrl_error(s, "SUBSCRIPT OUT OF RANGE");
                return 0;
            }

            return s->var.arr[vn][idx];
        }

        if (is_str)
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        return s->var.val[vn];
    }

    /* Parenthesized expression */
    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
    {
        lexer_next(s);
        int v = expr(s, 1);

        if (s->ctrl.stopped)
            return 0;

        if (s->lex.type != T_SYM || s->lex.buf[0] != ')')
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        lexer_next(s);
        return v;
    }

    /* Built-in functions: PEEK, ABS, SGN, RND */
    if (s->lex.type == T_KEY)
    {
        int fn = s->lex.kw;

        lexer_next(s);

        if (s->lex.type != T_SYM || s->lex.buf[0] != '(')
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        lexer_next(s);
        int arg = expr(s, 1);

        if (s->ctrl.stopped)
            return 0;

        if (s->lex.type != T_SYM || s->lex.buf[0] != ')')
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        lexer_next(s);

        switch (fn)
        {
        case K_PEEK:
            return *(volatile uint8_t *)arg;
        case K_ABS:
            return arg < 0 ? -arg : arg;
        case K_SGN:
            return arg < 0 ? -1 : arg > 0 ? 1 : 0;
        case K_RND:
            return arg > 0 ? (rand() % arg) + 1 : 0;
        default:
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }
    }

    /* User-defined function FNX */
    if (s->lex.type == T_FN)
    {
        int fn_idx = s->lex.num;

        lexer_next(s);

        if (s->lex.type != T_SYM || s->lex.buf[0] != '(')
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        lexer_next(s);
        int arg = expr(s, 1);

        if (s->ctrl.stopped)
            return 0;

        if (s->lex.type != T_SYM || s->lex.buf[0] != ')')
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        lexer_next(s);

        if (s->fn.param_var_idx[fn_idx] < 0)
        {
            ctrl_error(s, "UNDEFINED FUNCTION");
            return 0;
        }

        BasicLex saved = s->lex;
        int      param = s->fn.param_var_idx[fn_idx];
        int      old = s->var.val[param];

        s->var.val[param] = arg;
        s->lex.ptr = s->fn.body[fn_idx];
        lexer_next(s);
        int result = expr(s, 1);
        s->var.val[param] = old;
        s->lex = saved;
        return result;
    }

    ctrl_error(s, "SYNTAX ERROR");
    return 0;
}

/* Pratt parser — operator precedence climbing */

static int expr(BasicState *s, int minprec)
{
    int n = primary(s);

    if (s->ctrl.stopped)
        return 0;

    for (;;)
    {
        /* Map current token to an operator key */
        int kw;

        if (s->lex.type == T_SYM)
            kw = s->lex.buf[0];
        else if (s->lex.type == T_KEY && (s->lex.kw == K_AND || s->lex.kw == K_OR ||
                                          s->lex.kw == K_MOD || s->lex.kw == K_NOT))
            kw = s->lex.kw;
        else
            break;

        int prec = op_prec(kw);

        if (prec < minprec)
            break;

        /* ^ is right-associative, everything else left-associative */
        int is_right = (kw == '^');

        lexer_next(s);

        int rhs = expr(s, is_right ? prec : prec + 1);

        if (s->ctrl.stopped)
            return 0;

        switch (kw)
        {
        case '+':
            n = n + rhs;
            break;
        case '-':
            n = n - rhs;
            break;
        case '*':
            n = n * rhs;
            break;
        case '/':
            n = rhs != 0 ? n / rhs : 0;
            break;
        case '\\':
            n = rhs != 0 ? n / rhs : 0;
            break;
        case K_MOD:
            n = rhs != 0 ? n % rhs : 0;
            break;
        case '^':
        {
            int base = n, exp = rhs, result = 1;

            while (exp > 0)
            {
                result *= base;
                exp--;
            }
            n = result;
            break;
        }
        case '=':
            n = (n == rhs);
            break;
        case '<':
            n = (n < rhs);
            break;
        case '>':
            n = (n > rhs);
            break;
        case K_AND:
            n = (n && rhs);
            break;
        case K_OR:
            n = (n || rhs);
            break;
        }
    }

    return n;
}

/* Public entry points */

int expr_eval(BasicState *s)
{
    return expr(s, 1);
}

int expr_parse_paren(BasicState *s)
{
    if (s->lex.type != T_SYM || s->lex.buf[0] != '(')
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    lexer_next(s);
    int v = expr(s, 1);

    if (s->ctrl.stopped)
        return 0;

    if (s->lex.type != T_SYM || s->lex.buf[0] != ')')
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    lexer_next(s);
    return v;
}
