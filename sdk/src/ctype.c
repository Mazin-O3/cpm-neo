/*
 * libc/ctype.c
 * CP/M Neo — Character classification and case conversion (software only)
 *
 * Definitions must match the prototypes in ctype.h exactly.  All functions
 * operate on the low 7-bit ASCII range and are safe for any int input.
 */

#include "ctype.h"

int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

int tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}
