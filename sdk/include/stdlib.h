#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include "abi.h"
#include <stdint.h>

void exit(int status) __attribute__((noreturn));
int  exec(const char *path, int argc, char **argv);
int  getargs(ArgBlock *out);

int atoi(const char *s);
int strtoi(const char *nptr, char **endptr, int base);

int  rand(void);
void srand(unsigned seed);

#endif
