#include "stdlib.h"
#include "syscall.h"

#include <ctype.h>

void exit(int status)
{
    sys_exit(status);
}

int exec(const char *path, int argc, char **argv)
{
    return sys_exec(path, argc, argv);
}

int getargs(ArgBlock *out)
{
    return sys_args(out);
}

int atoi(const char *s)
{
    int sign = 1, val = 0;

    while (isspace((unsigned char)*s))
        s++;

    if (*s == '+')
        s++;
    else if (*s == '-')
    {
        sign = -1;
        s++;
    }

    while (isdigit((unsigned char)*s))
        val = val * 10 + (*s++ - '0');

    return sign * val;
}

char *itoa(int value, char *str, int base)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char *p = str;
    int   neg = (value < 0) && (base == 10);
    unsigned int v = neg ? ((unsigned int)(-(value + 1)) + 1u) : (unsigned int)value;

    if (base < 2 || base > 36)
    {
        *str = '\0';

        return str;
    }

    do
    {
        *p++ = digits[v % (unsigned int)base];
        v /= (unsigned int)base;
    } while (v);

    if (neg)
        *p++ = '-';

    *p = '\0';

    for (char *a = str, *b = p - 1; a < b; a++, b--)
    {
        char t = *a;
        *a = *b;
        *b = t;
    }

    return str;
}

static uint32_t rnd_seed = 1;

void srand(uint32_t seed)
{
    rnd_seed = seed ? seed : (uint32_t)sys_millis();
}

int rand(void)
{
    rnd_seed = rnd_seed * 1103515245UL + 12345;
    return (int)((rnd_seed >> 16) & 0x7FFF);
}

void delay(uint32_t ms)
{
    uint32_t start = sys_millis();

    while (sys_millis() - start < ms)
        ;
}
