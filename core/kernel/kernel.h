#ifndef KERNEL_H
#define KERNEL_H

#include <errno.h>
#include "bdos.h"
#include "bios.h"
#include "kernel_abi.h"
#include <stdio.h>
#include <stdint.h>

extern char __kernel_base[];
extern char __io_base[];
extern char __tpa_base[];

/* Base address for execute-in-place (XIP). Defined by the linker via
 * --defsym. Zero on non-XIP platforms (never used). Declared as a char
 * array like __kernel_base so that (uintptr_t)XIP_BASE yields the link
 * constant (the flash-mapped window base), not a read of that memory. */
extern char XIP_BASE[];

/* Exclusive end of the XIP window [XIP_BASE, __xip_top) for XIP
 * execution (XIP_BASE + XIP_SIZE). Defined by the linker via --defsym;
 * zero on non-XIP platforms. Char-array form: the address is the constant. */
extern char __xip_top[];

int      kernel_init(void);
void     kexec_ccp(void) __attribute__((noreturn));

#endif /* KERNEL_H */
