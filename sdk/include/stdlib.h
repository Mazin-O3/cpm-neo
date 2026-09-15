#ifndef SDK_STDLIB_H
#define SDK_STDLIB_H

#include <fsctx.h>
#include <stdint.h>

void exit(int status) __attribute__((noreturn));
int  exec(const char *path, int argc, char **argv);
int  getargs(ArgBlock *out);

int  atoi(const char *s);
char *itoa(int value, char *str, int base);

int  rand(void);
void srand(uint32_t seed);

void delay(uint32_t ms);

#endif /* SDK_STDLIB_H */
