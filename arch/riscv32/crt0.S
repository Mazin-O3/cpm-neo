/*
 * arch/<isa>/crt0.S
 * CP/M Neo 32-bit C runtime startup
 *
 * Shared by the kernel, CCP, and every user program: sets the stack pointer
 * (and, on this ISA, the global pointer), copies .data from its load address
 * (_data_load, an absolute LMA) to its runtime address (_data_start, its VMA),
 * zeroes .bss, then jumps to the C entry point (_start in sdk/src/start.c).
 * _stack_top, _data_start, _data_end, _data_load, _bss_start, and _bss_end are
 * provided by the target linker script (linker_kernel.ld for the kernel,
 * linker_app.ld for programs).
 *
 * The .data source is the link-time constant _data_load, never derived from
 * the PC: every executable runs at its link origin (Δ = 0) by construction —
 * the bootloader jumps the kernel to its link base (RAM or XIP window), sysgen
 * places the CCP at exactly its link origin, and .com files load at the TPA
 * base.  On XIP builds .data's LMA sits in the flash window right past the
 * image's .text and crt0 copies it into RAM; on plain RAM builds LMA == VMA
 * and the copy is a self-copy no-op.
 */

.section .text.entry, "ax"
.global _entry
.align 2

_entry:
    /* Initialize Stack and Global Pointers */
    la   sp, _stack_top
    .option push
    .option norelax
    la   gp, _data_start + 0x800
    .option pop

    /* Source (.data LMA) — A fixed link-time address, absolute. */
    lui  t0, %hi(_data_load)
    addi t0, t0, %lo(_data_load)

    la   t1, _data_start           /* Destination (.data VMA) via t1 */
    la   t2, _data_end             /* End of .data VMA via t2 */
    bgeu t1, t2, .Ldata_done       /* Skip if start >= end */

.Ldata_loop:
    lw   t3, 0(t0)
    sw   t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    bltu t1, t2, .Ldata_loop       /* Continue while current < end */
.Ldata_done:

    /* Zero out the .bss section */
    la   t0, _bss_start            /* Destination (.bss VMA) via t0 */
    la   t1, _bss_end              /* End of .bss VMA via t1 */
    bgeu t0, t1, .Lbss_done        /* Skip if start >= end */

.Lbss_loop:
    sw   zero, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, .Lbss_loop        /* Continue while current < end */
.Lbss_done:

    /* Jump to C entry point */
    tail _start