#include "basic.h"

const char *lex_kw_names[] = {"LET",  "PRINT", "INPUT", "GOTO", "GOSUB", "RETURN", "IF",   "THEN",
                              "FOR",  "TO",    "STEP",  "NEXT", "END",   "REM",    "AND",  "OR",
                              "LIST", "LOAD",  "RUN",   "NEW",  "POKE",  "EXIT",   "PEEK", "ABS",
                              "SGN",  "RND",   "DEF",   "DIM",  "FRE",   "CLR",    "SAVE"};

int lex_kw_id(const char *w)
{
    int n = sizeof(lex_kw_names) / sizeof(lex_kw_names[0]);

    for (int i = 0; i < n; i++)

        if (!strcmp(w, lex_kw_names[i]))
            return i;
    return -1;
}

int lex_next(BasicState *s)
{
    while (*s->lex.p && (unsigned char)*s->lex.p <= ' ')
        s->lex.p++;

    if (!*s->lex.p)
    {
        s->lex.type = T_EOF;
        s->lex.buf[0] = 0;
        return 1;
    }

    if ((unsigned char)*s->lex.p >= TOK_BASE)
    {
        int kw = (unsigned char)*s->lex.p - TOK_BASE;
        strcpy(s->lex.buf, lex_kw_names[kw]);
        s->lex.type = T_KEY;
        s->lex.kw = kw;
        s->lex.p++;
        return 1;
    }

    if (isdigit((unsigned char)*s->lex.p))
    {
        int i = 0, v = 0;

        while (isdigit((unsigned char)s->lex.p[i]))
        {
            if (i < STR_SZ - 1)
                s->lex.buf[i] = s->lex.p[i];
            int d = s->lex.p[i] - '0';

            if (v > (INT_MAX - d) / 10)
                v = INT_MAX; /* Saturate rather than overflow */
            else
                v = v * 10 + d;
            i++;
        }

        s->lex.buf[i < STR_SZ - 1 ? i : STR_SZ - 1] = 0;
        s->lex.num = v;
        s->lex.p += i;
        s->lex.type = T_NUM;
        return 1;
    }

    if (isalpha((unsigned char)*s->lex.p))
    {
        int i = 0;

        while (isalpha((unsigned char)s->lex.p[i]) && i < STR_SZ - 1)
        {
            s->lex.buf[i] = toupper((unsigned char)s->lex.p[i]);
            i++;
        }

        s->lex.buf[i] = 0;
        s->lex.p += i;

        if (i == 1)
        {
            s->lex.type = T_VAR;
            s->lex.num = s->lex.buf[0] - 'A';

            if (*s->lex.p == '$')
            {
                s->lex.p++;
                s->lex.quote = 1;
            }
            else
                s->lex.quote = 0;
            return 1;
        }

        if (i == 3 && s->lex.buf[0] == 'F' && s->lex.buf[1] == 'N' && isalpha(s->lex.buf[2]))
        {
            s->lex.type = T_FN;
            s->lex.num = s->lex.buf[2] - 'A';
            return 1;
        }

        int k = lex_kw_id(s->lex.buf);

        if (k >= 0)
        {
            s->lex.type = T_KEY;
            s->lex.kw = k;
            return 1;
        }

        ctl_error(s, "SYNTAX ERROR");
        s->lex.type = T_EOF;
        s->ctl.stopped = 1;
        return 0;
    }

    if (*s->lex.p == '"')
    {
        s->lex.p++;
        int i = 0;

        while (*s->lex.p && *s->lex.p != '"' && i < 63)
            s->lex.buf[i++] = *s->lex.p++;
        s->lex.buf[i] = 0;

        if (*s->lex.p == '"')
        {
            s->lex.p++;
            s->lex.type = T_STR;
            return 1;
        }

        ctl_error(s, "SYNTAX ERROR");
        s->lex.type = T_EOF;
        s->ctl.stopped = 1;
        return 0;
    }

    s->lex.buf[0] = *s->lex.p;
    s->lex.buf[1] = 0;

    if ((*s->lex.p == '<' && s->lex.p[1] == '=') || (*s->lex.p == '>' && s->lex.p[1] == '=') ||
        (*s->lex.p == '<' && s->lex.p[1] == '>'))
    {
        s->lex.buf[1] = s->lex.p[1];
        s->lex.buf[2] = 0;
        s->lex.p += 2;
    }
    else
    {
        s->lex.p++;
    }

    s->lex.type = T_SYM;
    return 1;
}
