#ifndef KERNEL_H
#define KERNEL_H

#include "abi.h"
#include "bdos.h"
#include "bios.h"
#include "disk_format.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

extern char __kernel_base[];
extern char __io_base[];
extern char __tpa_base[];

/* Base address for execute-in-place (XIP). Defined by the linker via
 * --defsym. Zero on non-XIP platforms (never used). Declared as a char
 * array like __kernel_base so that (uintptr_t)XIP_BASE yields the link
 * constant (the flash-mapped window base), not a read of that memory. 
*/
extern char XIP_BASE[];

/* Transfer control to a program loaded at |addr|.  
 * Default is a weak C call (kernel.c).
 * ISAs with address-encoded execution state override it
 * with a strong arch/<isa>/kjump.S.  
*/
void kjump(uintptr_t addr);

int  kernel_init(void);
void kexec_ccp(void) __attribute__((noreturn));

#endif /* KERNEL_H */
