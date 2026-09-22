/*
 * apps/extra/basic/lex.c — Lexer and tokenizer
 *
 * Keyword table, source-to-token conversion (tokenize_line), program
 * entry traversal, and the streaming lexer (lexer_next) that feeds
 * tokens to the evaluator and statement executor.
 */

#include "basic.h"

#include <ctype.h>
#include <string.h>

/* Keyword table */

static const char *kw_names[] = {
    "LET",  "PRINT", "INPUT", "GOTO", "GOSUB", "RETURN", "IF",   "THEN", "FOR",  "TO",   "STEP",
    "NEXT", "END",   "REM",   "AND",  "OR",    "LIST",   "LOAD", "RUN",  "NEW",  "POKE", "EXIT",
    "PEEK", "ABS",   "SGN",   "RND",  "DEF",   "DIM",    "FRE",  "CLR",  "SAVE", "MOD",  "NOT",  "SQR"};

static int kw_count(void)
{
    return (int)(sizeof(kw_names) / sizeof(kw_names[0]));
}

int lexer_kw_id(const char *w)
{
    for (int i = 0; i < kw_count(); i++)
        if (w[0] == kw_names[i][0] && !strcmp(w, kw_names[i]))
            return i;
    return -1;
}

const char *lexer_kw_name(int kw)
{
    if (kw < 0 || kw >= kw_count())
        return "?";

    return kw_names[kw];
}

/* Tokenization */

/* Bytes >= BASIC_TOKEN_BASE mean "keyword token" in stored text, so any such
 * byte typed by the user (e.g. UTF-8) must never be stored verbatim — LIST,
 * SAVE and the lexer would all misread it as a keyword. */
static char tok_ch(char c)
{
    return (unsigned char)c >= BASIC_TOKEN_BASE ? '?' : c;
}

void tokenize_line(char *dst, unsigned max_dst, const char *src)
{
    unsigned n = 0;

    if (max_dst == 0)
        return;

    while (*src && n + 1 < max_dst)
    {
        if (*src == '"')
        {
            *dst++ = *src++;
            n++;

            while (*src && *src != '"' && n + 1 < max_dst)
            {
                *dst++ = tok_ch(*src++);
                n++;
            }

            if (*src == '"' && n + 1 < max_dst)
            {
                *dst++ = *src++;
                n++;
            }

            continue;
        }

        if (isalpha((unsigned char)*src))
        {
            char word[64];
            int  i = 0;

            while (i < 63 && src[i] && isalpha((unsigned char)src[i]))
            {
                word[i] = src[i];
                i++;
            }

            word[i] = 0;
            strupr(word);
            int kw = lexer_kw_id(word);

            if (kw >= 0)
            {
                if (n + 1 >= max_dst)
                    break;

                *dst++ = (char)(unsigned char)(BASIC_TOKEN_BASE + kw);
                n++;
                src += i;

                if (kw == K_REM)
                {
                    while (*src && n + 1 < max_dst)
                    {
                        *dst++ = tok_ch(*src++);
                        n++;
                    }

                    *dst = 0;
                    return;
                }
            }
            else
            {
                if (n + (unsigned)i >= max_dst)
                    break;

                memcpy(dst, src, i);
                dst += i;
                n += i;
                src += i;
            }

            continue;
        }

        if (n + 1 >= max_dst)
            break;

        *dst++ = tok_ch(*src++);
        n++;
    }
    *dst = 0;
}

/* Lexer helpers */

int lex_chk_sym(BasicState *s, char ch)
{
    if (s->lex.type != T_SYM || s->lex.buf[0] != ch)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    return 1;
}

int lex_expect_sym(BasicState *s, char ch)
{
    if (!lex_chk_sym(s, ch))
        return 0;

    return lexer_next(s) && !s->ctrl.stopped;
}

int lex_expect_key(BasicState *s, int kw)
{
    if (s->lex.type != T_KEY || s->lex.kw != kw)
    {
        ctrl_error(s, "SYNTAX ERROR");
        return 0;
    }

    return lexer_next(s) && !s->ctrl.stopped;
}


/* Lexer */

static int lex_fail(BasicState *s)
{
    ctrl_error(s, "SYNTAX ERROR");
    s->lex.type = T_EOF;
    s->lex.buf[0] = 0;
    s->ctrl.stopped = 1;
    return 0;
}

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

    /* Tokenized keyword */
    if ((unsigned char)*s->lex.ptr >= BASIC_TOKEN_BASE)
    {
        int kw = (unsigned char)*s->lex.ptr - BASIC_TOKEN_BASE;

        /* Raw (untokenized) direct-mode input can contain any byte. */
        if (kw >= kw_count())
            return lex_fail(s);

        strcpy(s->lex.buf, kw_names[kw]);
        s->lex.type = T_KEY;
        s->lex.kw = kw;
        s->lex.ptr++;
        return 1;
    }

    /* Number */
    if (isdigit((unsigned char)*s->lex.ptr))
    {
        int i = 0, v = 0;

        while (isdigit((unsigned char)s->lex.ptr[i]))
        {
            if (i < (int)sizeof(s->lex.buf) - 1)
                s->lex.buf[i] = s->lex.ptr[i];
            int d = s->lex.ptr[i] - '0';

            if (v > (INT_MAX - d) / 10)
                v = INT_MAX;
            else
                v = v * 10 + d;
            i++;
        }

        s->lex.buf[i < (int)sizeof(s->lex.buf) - 1 ? i : (int)sizeof(s->lex.buf) - 1] = 0;
        s->lex.num = v;
        s->lex.ptr += i;
        s->lex.type = T_NUM;
        return 1;
    }

    /* Identifier or keyword */
    if (isalpha((unsigned char)*s->lex.ptr))
    {
        int i = 0;

        while (isalpha((unsigned char)s->lex.ptr[i]) && i < (int)sizeof(s->lex.buf) - 1)
        {
            s->lex.buf[i] = s->lex.ptr[i];
            i++;
        }

        s->lex.buf[i] = 0;
        strupr(s->lex.buf);
        s->lex.ptr += i;

        /* Single letter = variable */
        if (i == 1)
        {
            int subs = 0;

            s->lex.type = T_VAR;

            /* One optional digit right after the letter: A1, X9 ... */
            if (isdigit((unsigned char)*s->lex.ptr))
            {
                subs = *s->lex.ptr - '0' + 1;
                s->lex.ptr++;
            }

            s->lex.num = (s->lex.buf[0] - 'A') * BASIC_SUBS + subs;

            if (*s->lex.ptr == '$')
            {
                /* String variables are letter-only. */
                if (subs)
                    return lex_fail(s);

                s->lex.ptr++;
                s->lex.is_string = 1;
            }
            else
                s->lex.is_string = 0;
            return 1;
        }

        /* FNX = user-defined function */
        if (i == 3 && s->lex.buf[0] == 'F' && s->lex.buf[1] == 'N' && isalpha(s->lex.buf[2]))
        {
            s->lex.type = T_FN;
            s->lex.num = s->lex.buf[2] - 'A';
            return 1;
        }

        /* Keyword */
        int k = lexer_kw_id(s->lex.buf);

        if (k >= 0)
        {
            s->lex.type = T_KEY;
            s->lex.kw = k;
            return 1;
        }

        return lex_fail(s);
    }

    /* String literal */
    if (*s->lex.ptr == '"')
    {
        s->lex.ptr++;
        int i = 0;

        while (*s->lex.ptr && *s->lex.ptr != '"')
        {
            if (i < (int)sizeof(s->lex.buf) - 1)
                s->lex.buf[i++] = *s->lex.ptr;
            s->lex.ptr++;
        }
        s->lex.buf[i] = 0;

        if (*s->lex.ptr == '"')
        {
            s->lex.ptr++;
            s->lex.type = T_STR;
            return 1;
        }

        return lex_fail(s);
    }

    /* Symbol (single or two-char: <=, >=, <>) */
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
        s->lex.ptr++;

    s->lex.type = T_SYM;
    return 1;
}

/* Discard the rest of the text.  Leaves the lexer at end-of-line so callers
 * never see a stale token afterwards. */
void lexer_skip_line(BasicState *s)
{
    s->lex.ptr += strlen(s->lex.ptr);
    s->lex.type = T_EOF;
    s->lex.buf[0] = 0;
}