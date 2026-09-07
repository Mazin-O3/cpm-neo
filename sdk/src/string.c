/*
 * libc/string.c
 * CP/M Neo — String and memory utilities (software only)
 */

#include "string.h"
#include "ctype.h"

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n--)
        *d++ = *s++;

    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else
    {
        d += n;
        s += n;

        while (n--)
            *(--d) = *(--s);
    }

    return dst;
}

void *memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)val;

    if (((size_t)d & 3) == 0 && n >= 4)
    {
        size_t v4 = (size_t)v | ((size_t)v << 8) | ((size_t)v << 16) | ((size_t)v << 24);
        size_t nw = (size_t)(n >> 2);
        do
        {
            *(size_t *)d = v4;
            d += 4;
        } while (--nw);
        n &= 3;
    }

    while (n--)
        *d++ = v;

    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;

    for (size_t i = 0; i < n; i++)
    {
        if (x[i] != y[i])
            return x[i] - y[i];
    }

    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n])
    {
        n++;
    }

    return n;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;

    do
    {
        if (*s == (char)c)
            last = s;
    } while (*s++);

    return (char *)last;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n > 0 && *a && *a == *b)
    {
        a++;
        b++;
        n--;
    }

    if (n == 0 || *a == *b)
    {
        return 0;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;

    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;

    while (n > 0 && *src != '\0')
    {
        *d++ = *src++;
        n--;
    }

    while (n > 0)
    {
        *d++ = '\0';
        n--;
    }

    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;

    while (*d)
        d++;

    while (n-- && *src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s != (char)c)
    {
        if (!*s++)
        {
            return NULL;
        }
    }

    return (char *)s;
}

void strupr(char *s)
{
    while (*s)
    {
        *s = (char)toupper((unsigned char)*s);
        s++;
    }
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        int da = toupper((unsigned char)a[i]);
        int db = toupper((unsigned char)b[i]);

        if (da != db)
            return da - db;

        if (a[i] == '\0')
            return 0;
    }

    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        int da = toupper((unsigned char)*a);
        int db = toupper((unsigned char)*b);

        if (da != db)
            return da - db;
        a++;
        b++;
    }

    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}
