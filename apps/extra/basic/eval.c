/*
 * apps/extra/basic/eval.c — Expression evaluator
 *
 * Recursive-descent Pratt parser with clean operator precedence.
 * Handles: + - * / \ MOD ^ = < > <= >= <> AND OR NOT unary -/+
 * String comparisons (A$="X", "A"<B$ ...) are parsed as a primary that
 * yields 0/1, so they combine with AND/OR/NOT like any other value.
 */

#include "basic.h"

#include <string.h>

/* Synthetic operator codes for the two-character comparison symbols.
 * They sit above 255 so they can never collide with a character or K_* id. */
#define OP_LE 256
#define OP_GE 257
#define OP_NE 258

/* Operator precedence — low binds loosest */

static int op_prec(int op)
{
    switch (op)
    {
    case K_OR:
        return 1;
    case K_AND:
        return 2;
    case '=':
    case '<':
    case '>':
    case OP_LE:
    case OP_GE:
    case OP_NE:
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

/* Map a symbol token (1 or 2 chars) to an operator code. */

static int sym_op(const char *b)
{
    if (b[0] == '<' && b[1] == '=')
        return OP_LE;
    if (b[0] == '>' && b[1] == '=')
        return OP_GE;
    if (b[0] == '<' && b[1] == '>')
        return OP_NE;
    return (unsigned char)b[0];
}

/* Wrapping arithmetic — signed overflow is undefined behaviour in C, and a
 * BASIC program should just wrap like a 16/32-bit machine would. */

static int w_add(int a, int b)
{
    return (int)((unsigned)a + (unsigned)b);
}

static int w_sub(int a, int b)
{
    return (int)((unsigned)a - (unsigned)b);
}

static int w_mul(int a, int b)
{
    return (int)((unsigned)a * (unsigned)b);
}

static int w_neg(int a)
{
    return (int)(0u - (unsigned)a);
}

/* Integer power by squaring; negative exponents follow integer rules. */

static int ipow(int base, int exp)
{
    unsigned result = 1u, b = (unsigned)base;

    if (exp < 0)
    {
        if (base == 1)
            return 1;
            
        if (base == -1)
            return (exp & 1) ? -1 : 1;
        return 0;
    }

    while (exp > 0)
    {
        if (exp & 1)
            result *= b;
        exp >>= 1;
        b *= b;
    }

    return (int)result;
}

/* Integer square root: floor(sqrt(n)) via Newton's method in integer
 * arithmetic (n / r truncates, matching classic BASIC's SQR-by-hand idiom).
 * Guards a 2-cycle oscillation, which plain Newton hits for some inputs when
 * done in integers instead of floats. Verified against every 0 <= n < 20000
 * and 20000 random large n. */

static int isqrt(BasicState *s, int n)
{
    if (n < 0)
    {
        ctrl_error(s, "ILLEGAL QUANTITY");
        return 0;
    }

    if (n == 0)
        return 0;

    int r = n;
    int prev = -1;

    for (int i = 0; i < 32; i++)
    {
        int nr = (r + n / r) / 2;

        if (nr == r)
            return r;

        if (nr == prev)
            return nr < r ? nr : r;

        prev = r;
        r = nr;
    }

    return r;
}

static int expr(BasicState *s, int minprec);

/* ( expr ) — current token must be '(' */

static int paren_expr(BasicState *s)
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

/* String comparison:  <str> <op> <str>   where str is a literal or A$ */

static int str_operand(BasicState *s, char *out)
{
    if (s->lex.type == T_STR)
        strncpy(out, s->lex.buf, BASIC_STR_LEN - 1);
    else if (s->lex.type == T_VAR && s->lex.is_string)
        strncpy(out, s->var.str[NAME_LETTER(s->lex.num)], BASIC_STR_LEN - 1);
    else
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    out[BASIC_STR_LEN - 1] = '\0';
    lexer_next(s);

    return !s->ctrl.stopped;
}

static int str_compare(BasicState *s)
{
    char a[BASIC_STR_LEN], b[BASIC_STR_LEN];

    if (!str_operand(s, a))
        return 0;

    if (s->lex.type != T_SYM ||
        (s->lex.buf[0] != '=' && s->lex.buf[0] != '<' && s->lex.buf[0] != '>'))
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    int op = sym_op(s->lex.buf);

    lexer_next(s);

    if (!str_operand(s, b))
        return 0;

    int c = strcmp(a, b);

    switch (op)
    {
    case '=':
        return c == 0;
    case '<':
        return c < 0;
    case '>':
        return c > 0;
    case OP_LE:
        return c <= 0;
    case OP_GE:
        return c >= 0;
    case OP_NE:
        return c != 0;
    default:
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }
}

/* Parse a primary expression: number, variable, array, (expr), unary,
 * NOT, built-in function, user function, string comparison. */

static int primary(BasicState *s)
{
    if (s->ctrl.stopped)
        return 0;

    /* Number literal */
    if (s->lex.type == T_NUM)
    {
        int v = s->lex.num;
        lexer_next(s);
        return v;
    }

    /* String comparison (the only place strings appear in an expression) */
    if (s->lex.type == T_STR || (s->lex.type == T_VAR && s->lex.is_string))
        return str_compare(s);

    /* Numeric variable or array element */
    if (s->lex.type == T_VAR)
    {
        int vn = s->lex.num;

        lexer_next(s);

        if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
        {
            int *ref = expr_array_ref(s, vn);

            return ref ? *ref : 0;
        }

        return s->var.val[vn];
    }

    /* Parenthesized expression */
    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
        return paren_expr(s);

    /* Unary minus / plus.  Operand binds at ^ level so -2^2 == -(2^2). */
    if (s->lex.type == T_SYM && (s->lex.buf[0] == '-' || s->lex.buf[0] == '+'))
    {
        int neg = s->lex.buf[0] == '-';

        lexer_next(s);
        int v = expr(s, 6);

        if (s->ctrl.stopped)
            return 0;

        return neg ? w_neg(v) : v;
    }

    /* NOT and built-in functions: PEEK, ABS, SGN, RND, FRE */
    if (s->lex.type == T_KEY)
    {
        int fn = s->lex.kw;

        if (fn == K_NOT)
        {
            /* Looser than comparisons, tighter than AND: NOT A=B == NOT (A=B) */
            lexer_next(s);
            int v = expr(s, 3);

            if (s->ctrl.stopped)
                return 0;

            return !v;
        }

        if (fn != K_PEEK && fn != K_ABS && fn != K_SGN && fn != K_RND && fn != K_FRE && fn != K_SQR)
        {
            ctrl_error(s, "SYNTAX ERROR");
            return 0;
        }

        lexer_next(s);
        int arg = paren_expr(s);

        if (s->ctrl.stopped)
            return 0;

        switch (fn)
        {
        case K_PEEK:
            return *BASIC_ADDR(arg);
        case K_ABS:
            return arg < 0 ? w_neg(arg) : arg;
        case K_SGN:
            return arg < 0 ? -1 : arg > 0 ? 1 : 0;
        case K_RND:
            return arg > 0 ? (rand() % arg) + 1 : 0;
        case K_FRE:
            return BASIC_PROG_MAX - (int)(s->prog.free_ptr - s->prog.data);
        case K_SQR:
            return isqrt(s, arg);
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
        int arg = paren_expr(s);

        if (s->ctrl.stopped)
            return 0;

        if (s->fn.param_var_idx[fn_idx] < 0 || !s->fn.body[fn_idx])
        {
            ctrl_error(s, "UNDEFINED FUNCTION");
            return 0;
        }

        if (s->fn.depth >= BASIC_FN_DEPTH)
        {
            ctrl_error(s, "FUNCTION NESTING TOO DEEP");
            return 0;
        }

        BasicLex saved = s->lex;
        int      param = s->fn.param_var_idx[fn_idx];
        int      old = s->var.val[param];

        s->fn.depth++;
        s->var.val[param] = arg;
        s->lex.ptr = s->fn.body[fn_idx];
        lexer_next(s);
        int result = expr(s, 1);

        if (!s->ctrl.stopped && s->lex.type != T_EOF)
            ctrl_error(s, "SYNTAX ERROR");

        s->var.val[param] = old;
        s->fn.depth--;
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
        int op;

        if (s->lex.type == T_SYM)
            op = sym_op(s->lex.buf);
        else if (s->lex.type == T_KEY &&
                 (s->lex.kw == K_AND || s->lex.kw == K_OR || s->lex.kw == K_MOD))
            op = s->lex.kw;
        else
            break;

        int prec = op_prec(op);

        if (prec < minprec)
            break;

        /* ^ is right-associative, everything else left-associative */
        int is_right = (op == '^');

        lexer_next(s);

        int rhs = expr(s, is_right ? prec : prec + 1);

        if (s->ctrl.stopped)
            return 0;

        switch (op)
        {
        case '+':
            n = w_add(n, rhs);
            break;
        case '-':
            n = w_sub(n, rhs);
            break;
        case '*':
            n = w_mul(n, rhs);
            break;
        case '/':
        case '\\':
            if (rhs == 0)
            {
                ctrl_error(s, "DIVISION BY ZERO");
                return 0;
            }
            n = (rhs == -1) ? w_neg(n) : n / rhs;
            break;
        case K_MOD:
            if (rhs == 0)
            {
                ctrl_error(s, "DIVISION BY ZERO");
                return 0;
            }
            n = (rhs == -1) ? 0 : n % rhs;
            break;
        case '^':
            if (n == 0 && rhs < 0)
            {
                ctrl_error(s, "DIVISION BY ZERO");
                return 0;
            }
            n = ipow(n, rhs);
            break;
        case '=':
            n = (n == rhs);
            break;
        case '<':
            n = (n < rhs);
            break;
        case '>':
            n = (n > rhs);
            break;
        case OP_LE:
            n = (n <= rhs);
            break;
        case OP_GE:
            n = (n >= rhs);
            break;
        case OP_NE:
            n = (n != rhs);
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

/*
 * Array element reference.  The current token must be the '(' right after the
 * array name.  Parses "(i)" or "(i,j)", checks that the array exists, that the
 * number of subscripts matches how it was DIMensioned, and that each subscript
 * is in range, and consumes through the ')'.  Returns a pointer to the element
 * in the pool, or NULL after reporting an error.
 *
 * Array reads, array assignment and INPUT all come through here, so these
 * checks exist exactly once.
 */

int *expr_array_ref(BasicState *s, int vn)
{
    int idx[2];
    int n = 0;

    /* Arrays are letter-only: A(3) is an array, A1(3) is not (yet) supported. */
    if (!NAME_PLAIN(vn))
    {
        ctrl_error(s, "SYNTAX ERROR");
        return NULL;
    }

    vn = NAME_LETTER(vn);

    lexer_next(s);                      /* consume '(' */

    for (;;)
    {
        idx[n++] = expr(s, 1);

        if (s->ctrl.stopped)
            return NULL;

        if (n < 2 && s->lex.type == T_SYM && s->lex.buf[0] == ',')
        {
            lexer_next(s);
            continue;
        }

        break;
    }

    if (s->lex.type != T_SYM || s->lex.buf[0] != ')')
    {
        ctrl_error(s, "SYNTAX ERROR");
        return NULL;
    }

    lexer_next(s);

    int rows = s->var.a_rows[vn];
    int cols = s->var.a_cols[vn];

    if (rows == 0)
    {
        ctrl_error(s, "UNDIMENSIONED ARRAY");
        return NULL;
    }

    if ((n == 2) != (cols != 0))
    {
        ctrl_error(s, "WRONG NUMBER OF SUBSCRIPTS");
        return NULL;
    }

    if (idx[0] < 0 || idx[0] >= rows || (n == 2 && (idx[1] < 0 || idx[1] >= cols)))
    {
        ctrl_error(s, "SUBSCRIPT OUT OF RANGE");
        return NULL;
    }

    return &s->var.pool[s->var.a_base[vn] + (n == 2 ? idx[0] * cols + idx[1] : idx[0])];
}

/* Public entry points */

int expr_eval(BasicState *s)
{
    return expr(s, 1);
}

int expr_parse_paren(BasicState *s)
{
    return paren_expr(s);
}