#include "basic.h"

int ctl_break_key(BasicState *s)
{
    if (!peekchar())
        return 0;

    int c = getchar();

    if (c == CH_ESC || c == CH_BREAK)
    {
        s->ctl.stopped = 1;
        return 1;
    }

    return 0;
}

void lex_skip_line(BasicState *s)
{
    while (*s->lex.p)
        s->lex.p++;
}

void exec_syntax_err(BasicState *s)
{
    ctl_error(s, "SYNTAX ERROR");

    lex_skip_line(s);

    s->lex.type = T_EOF;
}

void exec_line(BasicState *s, const char *t)
{
    if (ctl_break_key(s))
    {
        printf("\n");
        return;
    }

    char *saved_ci = s->ctl.ip;

    if (s->loop.resume)
    {
        s->lex.p = s->loop.resume;
        s->loop.resume = 0;
    }
    else
        s->lex.p = (char *)t;

    if (!lex_next(s))
        return;

    while (s->lex.type != T_EOF && !s->ctl.stopped)
    {
        if (s->lex.type == T_SYM && s->lex.buf[0] == ':')
        {
            lex_next(s);
            continue;
        }

        exec_stmt(s);

        if (s->ctl.ip != saved_ci || s->loop.resume)
        {
            lex_skip_line(s);
            s->lex.type = T_EOF;
        }
        else if (!*s->lex.p)
            s->lex.type = T_EOF;
    }
}

/* Helpers */

static int lex_chk_sym(BasicState *s, char ch)
{
    if (s->lex.type != T_SYM || s->lex.buf[0] != ch)
    {
        exec_syntax_err(s);
        return 0;
    }

    return 1;
}

static int lex_expect_sym(BasicState *s, char ch)
{
    if (!lex_chk_sym(s, ch))
        return 0;

    return lex_next(s) && !s->ctl.stopped;
}

static int lex_expect_key(BasicState *s, int kw)
{
    if (s->lex.type != T_KEY || s->lex.kw != kw)
    {
        exec_syntax_err(s);
        return 0;
    }

    return lex_next(s) && !s->ctl.stopped;
}

static void assign_variable(BasicState *s, int vn, int is_str)
{
    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
    {
        if (is_str)
        {
            exec_syntax_err(s);
            return;
        }

        int idx = expr_paren(s);

        if (s->ctl.stopped)
            return;

        if (!lex_expect_sym(s, '='))
            return;

        if (var_aset(s, vn, idx, expr_eval(s)) < 0)
            return;
    }
    else
    {
        if (!lex_expect_sym(s, '='))
            return;

        if (is_str)
        {
            if (!var_read_str(s, s->var.str[vn]))
            {
                exec_syntax_err(s);
                return;
            }
        }
        else
            s->var.val[vn] = expr_eval(s);
    }
}

/* Statement handlers */

static void exec_let(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_VAR)
    {
        exec_syntax_err(s);
        return;
    }

    assign_variable(s, s->lex.num, s->lex.quote);
}

static void exec_implicit_var(BasicState *s)
{
    int vn = s->lex.num;

    int is_str = s->lex.quote;

    if (!lex_next(s))
        return;

    if ((s->lex.type == T_SYM && s->lex.buf[0] == '(') ||
        (s->lex.type == T_SYM && s->lex.buf[0] == '='))
        assign_variable(s, vn, is_str);
    else
        exec_syntax_err(s);
}

static void exec_poke(BasicState *s)
{
    if (!lex_next(s))
        return;

    int a = expr_eval(s);

    if (s->ctl.stopped)
        return;

    if (!lex_expect_sym(s, ','))
        return;

    int v = expr_eval(s);

    if (s->ctl.stopped)
        return;

    *((volatile uint8_t *)a) = (uint8_t)v; /* Byte-width, matches PEEK */
}

static void exec_print(BasicState *s)
{
    if (!lex_next(s))
        return;

    int no_nl = 0;

    for (;;)
    {
        if (s->lex.type == T_VAR && s->lex.quote)
        {
            printf("%s", s->var.str[s->lex.num]);
            lex_next(s);
        }
        else if (s->lex.type == T_STR)
        {
            printf("%s", s->lex.buf);
            lex_next(s);
        }
        else if (s->lex.type == T_SYM && s->lex.buf[0] == ':')
        {
            break;
        }
        else if (s->lex.type != T_EOF)
        {
            int v = expr_eval(s);

            if (s->ctl.stopped)
                break;
            printf("%d", v);
        }
        else
        {
            break;
        }

        no_nl = 0;

        if (s->lex.type == T_SYM && s->lex.buf[0] == ';')
        {
            no_nl = 1;
            lex_next(s);
        }
        else if (s->lex.type == T_SYM && s->lex.buf[0] == ',')
        {
            printf("\t");
            lex_next(s);
        }
        else
        {
            break;
        }
    }

    if (!no_nl && !s->ctl.stopped)
        putchar('\n');
}

static void exec_input(BasicState *s)
{
    if (!lex_next(s))
        return;

    int prompt_shown = 0;

    if (s->lex.type == T_STR)
    {
        printf("%s", s->lex.buf);

        prompt_shown = 1;

        lex_next(s);

        if (s->lex.type == T_SYM && s->lex.buf[0] == ';')
            lex_next(s);
    }

    if (s->lex.type != T_VAR)
    {
        exec_syntax_err(s);
        return;
    }

    int vn = s->lex.num, is_str = s->lex.quote;

    lex_next(s);

    int idx = -1;

    if (s->lex.type == T_SYM && s->lex.buf[0] == '(')
    {
        if (!lex_expect_sym(s, '('))
            return;

        idx = expr_eval(s);

        if (s->ctl.stopped)
            return;

        if (!lex_expect_sym(s, ')'))
            return;

        if (s->var.dim[vn] == 0)
        {
            ctl_error(s, "UNDIMENSIONED ARRAY");
            return;
        }

        if (idx < 0 || idx >= s->var.dim[vn])
        {
            ctl_error(s, "SUBSCRIPT OUT OF RANGE");
            return;
        }
    }

    if (prompt_shown)
        printf("\n");

    printf("? ");

    char ibuf[STR_SZ];

    getline(ibuf, STR_SZ);

    if (is_str)
        strcpy(s->var.str[vn], ibuf);
    else if (idx >= 0)
        s->var.arr[vn][idx] = atoi(ibuf);
    else
        s->var.val[vn] = atoi(ibuf);
}

static void exec_goto(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_NUM)
    {
        exec_syntax_err(s);
        return;
    }

    char *idx = prog_find_line(s, s->lex.num);

    if (!idx)
        ctl_error(s, "UNDEFINED LINE");
    else
        s->ctl.ip = idx;
}

static void exec_gosub(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_NUM)
    {
        exec_syntax_err(s);
        return;
    }

    char *idx = prog_find_line(s, s->lex.num);

    if (!idx)
    {
        ctl_error(s, "UNDEFINED LINE");
        return;
    }

    if (s->gosub.sp >= GS_MAX - 1)
    {
        ctl_error(s, "GOSUB OVERFLOW");
        return;
    }

    s->gosub.stk[++s->gosub.sp] = entry_next(s->ctl.ip);

    s->ctl.ip = idx;
}

static void exec_return(BasicState *s)
{
    if (s->gosub.sp < 0)
    {
        ctl_error(s, "RETURN WITHOUT GOSUB");
        return;
    }

    s->ctl.ip = s->gosub.stk[s->gosub.sp--];
}

static void exec_if(BasicState *s)
{
    if (!lex_next(s))
        return;

    int cond;

    if (s->lex.type == T_STR || (s->lex.type == T_VAR && s->lex.quote))
    {
        char s1[STR_SZ], s2[STR_SZ];

        if (!var_read_str(s, s1))
        {
            exec_syntax_err(s);
            return;
        }

        if (s->lex.type != T_SYM ||
            (s->lex.buf[0] != '=' && s->lex.buf[0] != '<' && s->lex.buf[0] != '>'))
        {
            exec_syntax_err(s);
            return;
        }

        char op0 = s->lex.buf[0], op1 = s->lex.buf[1];

        lex_next(s);

        if (!var_read_str(s, s2))
        {
            exec_syntax_err(s);
            return;
        }

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
            exec_syntax_err(s);
            return;
        }
    }
    else
        cond = expr_eval(s);

    if (s->ctl.stopped)
        return;

    if (!lex_expect_key(s, K_THEN))
        return;

    if (cond)
    {
        if (s->lex.type == T_NUM)
        {
            char *idx = prog_find_line(s, s->lex.num);

            if (!idx)
                ctl_error(s, "UNDEFINED LINE");
            else
                s->ctl.ip = idx;
        }
        else
            exec_stmt(s);
    }
    else
        lex_skip_line(s);
}

static void exec_for(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_VAR)
    {
        exec_syntax_err(s);
        return;
    }

    int vn = s->lex.num;

    lex_next(s);

    if (!lex_expect_sym(s, '='))
        return;

    s->var.val[vn] = expr_eval(s);

    if (s->ctl.stopped)
        return;

    if (!lex_expect_key(s, K_TO))
        return;

    int end = expr_eval(s);

    if (s->ctl.stopped)
        return;

    int step = 1;

    if (s->lex.type == T_KEY && s->lex.kw == K_STEP)
    {
        lex_next(s);

        step = expr_eval(s);
    }

    if (s->loop.sp >= FOR_MAX - 1)
    {
        ctl_error(s, "FOR OVERFLOW");
        return;
    }

    s->loop.sp++;
    s->loop.var[s->loop.sp] = vn;
    s->loop.tgt[s->loop.sp] = end;
    s->loop.step[s->loop.sp] = step;
    s->loop.ret_ip[s->loop.sp] = s->ctl.ip;
    s->loop.ret_src[s->loop.sp] = s->lex.p;
}

static void exec_next(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_VAR)
    {
        exec_syntax_err(s);
        return;
    }

    int vn = s->lex.num;

    if (s->loop.sp < 0 || s->loop.var[s->loop.sp] != vn)
    {
        ctl_error(s, "NEXT WITHOUT FOR");
        return;
    }

    if (ctl_break_key(s))
        return;

    s->var.val[vn] += s->loop.step[s->loop.sp];

    if ((s->loop.step[s->loop.sp] > 0 && s->var.val[vn] > s->loop.tgt[s->loop.sp]) ||
        (s->loop.step[s->loop.sp] < 0 && s->var.val[vn] < s->loop.tgt[s->loop.sp]))
    {
        s->loop.sp--;

        lex_next(s);
    }
    else
    {
        if (!s->loop.ret_ip[s->loop.sp])
        {
            s->lex.p = s->loop.ret_src[s->loop.sp];

            lex_next(s);
        }
        else
        {
            s->loop.resume = s->loop.ret_src[s->loop.sp];

            s->ctl.ip = s->loop.ret_ip[s->loop.sp];
        }
    }
}

static void exec_end(BasicState *s)
{
    s->ctl.stopped = 1;
}

static void exec_dim(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_VAR || s->lex.quote)
    {
        exec_syntax_err(s);
        return;
    }

    int vn = s->lex.num;

    lex_next(s);

    if (!lex_expect_sym(s, '('))
        return;

    int size = expr_eval(s);

    if (s->ctl.stopped)
        return;

    if (!lex_expect_sym(s, ')'))
        return;

    if (size < 1 || size > DIM_MAX)
    {
        ctl_error(s, "BAD DIMENSION");
        return;
    }

    s->var.dim[vn] = size + 1;

    for (int i = 0; i <= size; i++)
        s->var.arr[vn][i] = 0;
}

static void exec_def(BasicState *s)
{
    if (!lex_next(s))
        return;

    if (s->lex.type != T_FN)
    {
        exec_syntax_err(s);
        return;
    }

    int fn_idx = s->lex.num;

    lex_next(s);

    if (!lex_expect_sym(s, '('))
        return;

    if (s->lex.type != T_VAR)
    {
        exec_syntax_err(s);
        return;
    }

    s->fn.param[fn_idx] = s->lex.num;

    lex_next(s);

    if (!lex_expect_sym(s, ')'))
        return;

    if (!lex_chk_sym(s, '='))
        return;

    /* Snapshot the body text into storage owned by the interpreter: the
     * source may be a direct-mode stack buffer or tokenized program text
     * that later memmoves, so neither pointer can be kept. */

    if (strlen(s->lex.p) >= sizeof(s->fn.text[0]))
    {
        ctl_error(s, "FUNCTION BODY TOO LONG");
        return;
    }

    strcpy(s->fn.text[fn_idx], s->lex.p);

    s->fn.body[fn_idx] = s->fn.text[fn_idx];

    lex_skip_line(s);
}

/* Main dispatch */

void exec_stmt(BasicState *s)
{
    if (s->lex.type == T_EOF)
        return;

    if (s->lex.type == T_VAR)
    {
        exec_implicit_var(s);
    }
    else if (s->lex.type != T_KEY)
    {
        exec_syntax_err(s);
    }
    else
    {
        switch (s->lex.kw)
        {
        case K_LET:
            exec_let(s);
            break;
        case K_REM:
            lex_skip_line(s);
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
            exec_end(s);
            break;
        case K_DIM:
            exec_dim(s);
            break;
        case K_DEF:
            exec_def(s);
            break;
        default:
            exec_syntax_err(s);
            break;
        }
    }
}
