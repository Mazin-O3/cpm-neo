#include "basic.h"

/* Keyword table */

static const char *kw_names[] = {
    "LET",  "PRINT", "INPUT", "GOTO", "GOSUB", "RETURN", "IF",   "THEN", "FOR", "TO",   "STEP",
    "NEXT", "END",   "REM",   "AND",  "OR",    "LIST",   "LOAD", "RUN",  "NEW", "POKE", "EXIT",
    "PEEK", "ABS",   "SGN",   "RND",  "DEF",   "DIM",    "FRE",  "CLR",  "SAVE"};

static int kw_count(void)
{
    return (int)(sizeof(kw_names) / sizeof(kw_names[0]));
}

int lexer_kw_id(const char *w)
{
    for (int i = 0; i < kw_count(); i++)
        if (!strcmp(w, kw_names[i]))
            return i;
    return -1;
}

const char *lexer_kw_name(int kw)
{
    if (kw < 0 || kw >= kw_count())
        return "?";

    return kw_names[kw];
}

/* Lexer */

int lexer_next(BasicState *s)
{
    while (*s->lex.ptr && (unsigned char)*s->lex.ptr <= ' ')
        s->lex.ptr++;

    if (!*s->lex.ptr)
    {
        s->lex.type = T_EOF;
        s->lex.buf[0] = 0;
        return 1;
    }

    if ((unsigned char)*s->lex.ptr >= BASIC_TOKEN_BASE)
    {
        int kw = (unsigned char)*s->lex.ptr - BASIC_TOKEN_BASE;
        strcpy(s->lex.buf, kw_names[kw]);
        s->lex.type = T_KEY;
        s->lex.kw = kw;
        s->lex.ptr++;
        return 1;
    }

    if (isdigit((unsigned char)*s->lex.ptr))
    {
        int i = 0, v = 0;

        while (isdigit((unsigned char)s->lex.ptr[i]))
        {
            if (i < BASIC_STR_LEN - 1)
                s->lex.buf[i] = s->lex.ptr[i];
            int d = s->lex.ptr[i] - '0';

            if (v > (INT_MAX - d) / 10)
                v = INT_MAX; /* Saturate rather than overflow */
            else
                v = v * 10 + d;
            i++;
        }

        s->lex.buf[i < BASIC_STR_LEN - 1 ? i : BASIC_STR_LEN - 1] = 0;
        s->lex.num = v;
        s->lex.ptr += i;
        s->lex.type = T_NUM;
        return 1;
    }

    if (isalpha((unsigned char)*s->lex.ptr))
    {
        int i = 0;

        while (isalpha((unsigned char)s->lex.ptr[i]) && i < BASIC_STR_LEN - 1)
        {
            s->lex.buf[i] = s->lex.ptr[i];
            i++;
        }

        s->lex.buf[i] = 0;
        strupr(s->lex.buf);
        s->lex.ptr += i;

        if (i == 1)
        {
            s->lex.type = T_VAR;
            s->lex.num = s->lex.buf[0] - 'A';

            if (*s->lex.ptr == '$')
            {
                s->lex.ptr++;
                s->lex.is_string = 1;
            }
            else
                s->lex.is_string = 0;
            return 1;
        }

        if (i == 3 && s->lex.buf[0] == 'F' && s->lex.buf[1] == 'N' && isalpha(s->lex.buf[2]))
        {
            s->lex.type = T_FN;
            s->lex.num = s->lex.buf[2] - 'A';
            return 1;
        }

        int k = lexer_kw_id(s->lex.buf);

        if (k >= 0)
        {
            s->lex.type = T_KEY;
            s->lex.kw = k;
            return 1;
        }

        ctrl_error(s, "SYNTAX ERROR");
        s->lex.type = T_EOF;
        s->ctrl.stopped = 1;
        return 0;
    }

    if (*s->lex.ptr == '"')
    {
        s->lex.ptr++;
        int i = 0;

        while (*s->lex.ptr && *s->lex.ptr != '"' && i < 63)
            s->lex.buf[i++] = *s->lex.ptr++;
        s->lex.buf[i] = 0;

        if (*s->lex.ptr == '"')
        {
            s->lex.ptr++;
            s->lex.type = T_STR;
            return 1;
        }

        ctrl_error(s, "SYNTAX ERROR");
        s->lex.type = T_EOF;
        s->ctrl.stopped = 1;
        return 0;
    }

    s->lex.buf[0] = *s->lex.ptr;
    s->lex.buf[1] = 0;

    if ((*s->lex.ptr == '<' && s->lex.ptr[1] == '=') ||
        (*s->lex.ptr == '>' && s->lex.ptr[1] == '=') ||
        (*s->lex.ptr == '<' && s->lex.ptr[1] == '>'))
    {
        s->lex.buf[1] = s->lex.ptr[1];
        s->lex.buf[2] = 0;
        s->lex.ptr += 2;
    }
    else
    {
        s->lex.ptr++;
    }

    s->lex.type = T_SYM;
    return 1;
}
