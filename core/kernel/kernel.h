#ifndef KERNEL_H
#define KERNEL_H

#include <errno.h>
#include "bdos.h"
#include "bios.h"
#include "abi.h"
#include "disk_format.h"
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

/* Transfer control to a program loaded at |addr|.  Per-architecture
 * assembly (arch/$CONFIG_ARCH/kjump.S) owns ISA-specific calling state,
 * e.g. the ARM Thumb bit.  Mirrors the classic CP/M BIOS jump-to-CCP
 * convention; matches the OS's multi-boot entry addresses (__tpa_base,
 * XIP CCP entry) which are storage origins, not call targets. */
void kjump(uintptr_t addr);

int      kernel_init(void);
void     kexec_ccp(void) __attribute__((noreturn));

#endif /* KERNEL_H */
