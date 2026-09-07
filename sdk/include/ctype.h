/*
 * libc/ctype.h
 * CP/M Neo — Character classification and case conversion
 *
 * These predicates and converters operate on ASCII characters passed as an
 * int.  They only inspect the low 7-bit ASCII range, so any int (including
 * negative ones, as produced by getchar() at EOF) is safe to pass through —
 * non-classified characters simply return 0 / pass through unchanged.
 *
 * Prototypes must match the definitions in ctype.c exactly.
 */

#ifndef CTYPE_H
#define CTYPE_H

/* Convert an ASCII letter to uppercase.  Non-letters pass through unchanged. */
int toupper(int c);

/* Convert an ASCII letter to lowercase.  Non-letters pass through unchanged. */
int tolower(int c);

/* Return nonzero if c is an ASCII letter (A-Z or a-z). */
int isalpha(int c);

/* Return nonzero if c is an ASCII digit (0-9). */
int isdigit(int c);

/* Return nonzero if c is an ASCII letter or digit (alphanumeric). */
int isalnum(int c);

#endif /* CTYPE_H */
