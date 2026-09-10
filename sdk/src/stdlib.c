#include "stdlib.h"
#include "syscall.h"

#include <limits.h>
#include <stddef.h>

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
    return strtoi(s, NULL, 10);
}

int strtoi(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    int         sign = 1, val = 0;

    while (*p == ' ' || *p == '\t')
        p++;

    if (*p == '+')
        p++;
    else if (*p == '-')
    {
        sign = -1;
        p++;
    }

    if (base == 0)
    {
        if (*p == '0')
        {
            p++;

            if (*p == 'x' || *p == 'X')
            {
                base = 16;
                p++;
            }
            else
            {
                base = 8;
            }
        }
        else
            base = 10;
    }

    int ovf = 0;

    while (1)
    {
        int d;

        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (*p >= 'a' && *p <= 'f')
            d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F')
            d = *p - 'A' + 10;
        else
            break;

        if (d >= base)
            break;

        if (!ovf && val > (INT_MAX - d) / base)
            ovf = 1;
        else if (!ovf)
            val = val * base + d;
        p++;
    }

    if (endptr)
        *endptr = (char *)p;

    if (ovf)
        return sign > 0 ? INT_MAX : INT_MIN;

    return val * sign;
}

static unsigned long rnd_seed = 1;

void srand(unsigned seed)
{
    rnd_seed = seed ? seed : (unsigned)sys_time();
}

int rand(void)
{
    rnd_seed = rnd_seed * 1103515245UL + 12345;
    return (int)((rnd_seed >> 16) & 0x7FFF);
}
