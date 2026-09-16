/*
 * kernel/main.c
 * CP/M Neo — Kernel C entry point
 */

#include "kernel.h"
#include <syscall.h>

/* Boot-time banner: run the transient SYS command for system info. */
static void print_system_info(void)
{
    char *argv[] = {"SYS"};

    sys_exec("SYS", 1, argv);
}

/*
 * Kernel entry point, called by the platform bootloader after the
 * baseline init.  Initializes the OS, prints system info, then hands
 * control to the CCP.  Never returns.
 */
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

/* C entry point.  Reached via crt0's `tail _start` after data/bss
 * bootstrap (the bootloader jumps to crt0._entry). */
void __attribute__((used, noinline)) _start(void)
{
    os_entry();

    for (;;)
        ;
}