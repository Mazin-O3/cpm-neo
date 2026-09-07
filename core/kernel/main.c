/*
 * kernel/main.c
 * CP/M Neo — Kernel C entry point
 */

#include "kernel.h"
#include <syscall.h>

static void print_system_info(void)
{
    char *argv[] = {"SYS"};

    sys_exec("SYS", 1, argv);
}

void os_entry(void)
{
    if (kernel_init() != EOK)
    {
        puts(" \nVOL ERR");

        while (1)
            ;
    }

    print_system_info();
    kexec_ccp();
}

void __attribute__((used, noinline)) _start(void)
{
    os_entry();

    for (;;)
        ;
}
