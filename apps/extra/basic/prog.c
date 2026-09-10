#include "basic.h"

void tokenize_line(char *dst, unsigned max_dst, const char *src)
{
    unsigned n = 0;

    while (*src && n + 1 < max_dst)
    {
        if (*src == '"')
        {
            *dst++ = *src++;
            n++;

            while (*src && *src != '"' && n + 1 < max_dst)
            {
                *dst++ = *src++;
                n++;
            }

            if (*src == '"' && n + 1 < max_dst)
            {
                *dst++ = *src++;
                n++;
            }

            continue;
        }

        if (isalpha(*src))
        {
            char word[64];
            int  i = 0;

            while (i < 63 && src[i] && isalpha(src[i]))
            {
                word[i] = toupper(src[i]);
                i++;
            }

            word[i] = 0;
            int kw = lex_kw_id(word);

            if (kw >= 0)
            {
                if (n + 1 >= max_dst)
                    break;
                *dst++ = (unsigned char)(TOK_BASE + kw);
                n++;
                src += i;

                if (kw == K_REM)
                {
                    while (*src && n + 1 < max_dst)
                    {
                        *dst++ = *src++;
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
        *dst++ = *src++;
        n++;
    }
    *dst = 0;
}

char *entry_next(char *p)
{
    p += 2;

    while (*p)
        p++;
    return p + 1;
}

char *prog_find_line(BasicState *s, int n)
{
    char *p = s->prog.data;

    while (p < s->prog.free)
    {
        int num = (unsigned char)p[0] | ((unsigned char)p[1] << 8);

        if (num == n)
            return p;
        p = entry_next(p);
    }

    return NULL;
}

void prog_del_line(BasicState *s, int n)
{
    char *p = prog_find_line(s, n);

    if (!p)
        return;

    char *next = entry_next(p);

    int rest = (int)(s->prog.free - next);

    memmove(p, next, rest);

    s->prog.free -= (int)(next - p);
}

void prog_add_line(BasicState *s, int n, const char *t)
{
    prog_del_line(s, n);

    if (!*t)
        return;

    char tokened[512];

    tokenize_line(tokened, sizeof(tokened), t);

    int len = 2 + strlen(tokened) + 1;

    char *prev = s->prog.data;
    char *ins = s->prog.data;

    while (ins < s->prog.free)
    {
        int num = (unsigned char)ins[0] | ((unsigned char)ins[1] << 8);

        if (num > n)
            break;
        prev = entry_next(ins);
        ins = prev;
    }

    if (s->prog.free + len > s->prog.data + MAX_TEXT)
    {
        printf("\n?PROGRAM FULL\n");
        return;
    }

    int rest = (int)(s->prog.free - ins);

    memmove(ins + len, ins, rest);

    ins[0] = n & 0xFF;

    ins[1] = (n >> 8) & 0xFF;

    memcpy(ins + 2, tokened, len - 2);

    s->prog.free += len;
}

void prog_list(BasicState *s)
{
    int rows = 0;

    uint8_t cw, ch;

    sys_consize(&cw, &ch);

    char *p = s->prog.data;

    while (p < s->prog.free)
    {
        int num = (unsigned char)p[0] | ((unsigned char)p[1] << 8);
        printf("%d ", num);
        const char *text = p + 2;

        while (*text)
        {
            if ((unsigned char)*text >= TOK_BASE)
            {
                printf("%s", lex_kw_names[(unsigned char)*text - TOK_BASE]);
                text++;
            }
            else
                putchar(*text++);
        }

        printf("\n");

        if (anykey("...", &rows, ch))
        {
            printf("\n");
            break;
        }

        p = entry_next(p);
    }
}

static void clear_vars_and_fns(BasicState *s)
{
    s->loop.sp = -1;

    s->gosub.sp = -1;

    for (int i = 0; i < NVARS; i++)
    {
        s->var.val[i] = 0;
        s->var.str[i][0] = 0;
        s->var.dim[i] = 0;
        s->fn.param[i] = -1;
        s->fn.body[i] = 0;
    }

    s->loop.resume = 0;
}

void prog_new(BasicState *s)
{
    clear_vars_and_fns(s);
    s->prog.free = s->prog.data;
    s->ctl.ip = NULL;
    s->ctl.stopped = 0;
    s->ctl.lineno = 0;
}

void clr_vars(BasicState *s)
{
    clear_vars_and_fns(s);
}

void prog_run(BasicState *s)
{
    if (s->prog.free == s->prog.data)
    {
        printf("\n?NO PROGRAM\n");
        return;
    }

    /* RUN clears variables but keeps DEF FN definitions (classic BASIC). */

    for (int i = 0; i < NVARS; i++)
    {
        s->var.val[i] = 0;
        s->var.str[i][0] = 0;
        s->var.dim[i] = 0;
    }

    s->loop.sp = -1;
    s->gosub.sp = -1;
    s->ctl.ip = s->prog.data;
    s->ctl.stopped = 0;
    s->loop.resume = 0;

    while (s->ctl.ip && s->ctl.ip < s->prog.free && !s->ctl.stopped)
    {
        char *cur = s->ctl.ip;

        s->ctl.lineno = (unsigned char)cur[0] | ((unsigned char)cur[1] << 8);

        exec_line(s, cur + 2);

        if (!s->ctl.stopped)
        {
            if (s->ctl.ip == cur && !s->loop.resume)
                s->ctl.ip = entry_next(cur);
        }
    }

    s->ctl.lineno = 0;
}
