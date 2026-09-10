/* arch/cortex-m/crt0.S
 * CP/M Neo 32-bit ARM C runtime startup
 *
 * Shared by the kernel, CCP, and every user program: sets the stack
 * pointer, copies .data from its load address (_data_load, an absolute
 * LMA) to its runtime address (_data_start, its VMA), zeroes .bss, then
 * branches to the C entry point (_start in sdk/src/start.c).
 * _stack_top, _data_start, _data_end, _data_load, _bss_start, and
 * _bss_end are provided by the target linker script (linker_kernel.ld
 * for the kernel, linker_app.ld for programs).
 *
 * The .data source is the link-time constant _data_load, never derived
 * from the PC: every executable runs at its link origin (Δ = 0) by
 * construction — the bootloader jumps the kernel to its link base (RAM
 * or XIP window), sysgen places the CCP at exactly its link origin, and
 * .com files load at the TPA base.  On XIP builds .data's LMA sits in
 * the flash window right past the image's .text and crt0 copies it into
 * RAM; on plain RAM builds LMA == VMA and the copy is a self-copy no-op.
 *
 * Cortex-M (ARMv7-M) executes only Thumb-2; every entry point here is a
 * Thumb function and the linker/branch interworking carries the bit0
 * convention (see kjump.S).
 */

.section .text.entry, "ax"
.align 2
.syntax unified
.thumb
.global _entry
.thumb_func

_entry:
    ldr sp, =_stack_top         /* Initial runtime stack */

    /* Source (.data LMA) — an absolute link-time constant. */
    ldr r0, =_data_load
    ldr r1, =_data_start        /* Destination (.data VMA) */
    ldr r2, =_data_end          /* End of .data VMA */
    cmp r1, r2
    bhs .Ldata_done             /* Skip if start >= end */

.Ldata_loop:
    ldr r3, [r0], #4
    str r3, [r1], #4
    cmp r1, r2
    blo .Ldata_loop             /* Continue while current < end */
.Ldata_done:

    /* Zero out the .bss section */
    ldr r0, =_bss_start         /* Destination (.bss VMA) */
    ldr r1, =_bss_end           /* End of .bss VMA */
    cmp r0, r1
    bhs .Lbss_done              /* Skip if start >= end */

    movs r2, #0
.Lbss_loop:
    str r2, [r0], #4
    cmp r0, r1
    blo .Lbss_loop              /* Continue while current < end */
.Lbss_done:

    bl _start