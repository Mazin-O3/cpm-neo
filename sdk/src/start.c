#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

extern int main(int argc, char **argv);

void __attribute__((used, noinline)) _start(void)
{
    ArgBlock args;
    getargs(&args);

    char *argv[args.argc + 1];

    for (int i = 0; i < args.argc; i++)
        argv[i] = args.argv[i];

    argv[args.argc] = NULL;

    exit(main(args.argc, argv));
}
