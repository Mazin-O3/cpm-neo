/*
 * arch/<isa>/crt0.S
 * CP/M Neo 32-bit C runtime startup
 *
 * Shared by the kernel, CCP, and every user program: sets the stack pointer
 * (and, on this ISA, the global pointer), zeroes .bss, then jumps to the C
 * entry point (_start in sdk/src/start.c).  _stack_top, _data_start,
 * _bss_start, and _bss_end are provided by the target linker script
 * (linker_kernel.ld for the kernel, linker_sdk.ld for programs).
 */

.section .text.entry, "ax"
.global _entry
.align 2

_entry:
    la   sp, _stack_top
    .option push
    .option norelax
    la   gp, _data_start + 0x800
    .option pop

    la   t0, _bss_start
    la   t1, _bss_end
Lclear:  sw   zero, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, Lclear

    tail _start
