/* I/O via FD_STDIN/FD_STDOUT. */

#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define EOF (-1)

/* Output */
int putchar(int c);
int puts(const char *s); /* Appends \n           */
int printf(const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Input */
int peekchar(void); /* Non-blocking: 1 = char ready, never consumes */
int getchar(void);
int getline(char *buf, int maxlen); /* Echo + backspace      */

/* Pause output after a full screen of lines: prints msg, waits for a key,
 * erases the prompt, returns 1 when the user pressed ESC.  `row` is a
 * caller-owned counter of lines printed since the last pause; `screen_rows`
 * is the console height (from sys_consize). */
int anykey(const char *msg, int *row, int screen_rows);

/* Error strings */
const char *strerror(int err);

#endif /* STDIO_H */
