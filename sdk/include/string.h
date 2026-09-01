/*
 * libc/string.h
 * CP/M Neo — string and memory utilities (software only)
 *
 * Prototypes here must match the definitions in string.c exactly.
 * Because these names collide with compiler-recognized builtins
 * (memcpy, memset, strcmp, ...), a mismatch is not just risky —
 * GCC/Clang treat it as a hard "conflicting types" error.
 *
 * Character classification and case conversion (toupper, tolower,
 * isalpha, isdigit, isalnum) live in <ctype.h>.
 */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

/* Memory blocks */
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
void   *memset(void *dst, int val, size_t n);
int     memcmp(const void *a, const void *b, size_t n);

/* Strings — copy, concatenate, and compare */
size_t  strlen(const char *s);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
char   *strncat(char *dst, const char *src, size_t n);
char   *strchr(const char *s, int c);
char   *strrchr(const char *s, int c);

/* Case-insensitive comparison */
void    strupr(char *s);                                   /* uppercase in place      */
int     strcasecmp(const char *a, const char *b);          /* case-insensitive compare */
int     strncasecmp(const char *a, const char *b, size_t n); /* first n chars           */

#endif /* STRING_H */
