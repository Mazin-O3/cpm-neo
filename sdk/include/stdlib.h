/*
 * sdk/include/stdlib.h — Process control and numeric conversion
 */
#ifndef SDK_STDLIB_H
#define SDK_STDLIB_H

#include <fsctx.h>
#include <stdint.h>

/*
 * Process control
 */
void exit(int status) __attribute__((noreturn));
int  exec(const char *path, int argc, char **argv);
int  getargs(ArgBlock *out);

/*
 * Numeric conversion
 */
int   atoi(const char *s);
char *itoa(int value, char *str, int base);

/*
 * Random
 */
int  rand(void);
void srand(uint32_t seed);

/*
 * Timer
 */
void delay(uint32_t ms);

#endif /* SDK_STDLIB_H */