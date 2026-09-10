/*
 * libc/limits.h
 * CP/M Neo — Implementation limits for the 32-bit int model
 *
 */

#ifndef LIMITS_H
#define LIMITS_H

#define INT_MAX 0x7FFFFFFF     /* Maximum value of an int          */
#define INT_MIN (-INT_MAX - 1) /* Minimum value of an int       */
#define UINT_MAX 0xFFFFFFFFu   /* Maximum value of an unsigned int */

#define CHAR_BIT 8      /* Bits in a char                   */
#define CHAR_MIN (-128) /* Minimum value of a signed char   */
#define CHAR_MAX 127    /* Maximum value of a signed char   */
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

#define SHRT_MIN (-32768) /* Minimum value of a short         */
#define SHRT_MAX 32767
#define USHRT_MAX 65535

#define LONG_MAX 0x7FFFFFFFL /* Long is 32-bit on rv32           */
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX 0xFFFFFFFFul

#endif /* LIMITS_H */
