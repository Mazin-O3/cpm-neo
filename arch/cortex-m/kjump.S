/* arch/cortex-m/kjump.S
 * CP/M Neo — Transfer control to a program loaded at |r0|.
 *
 * ARM Cortex-M executes only Thumb code: a branch target address must
 * have bit0 set, or the CPU raises a UsageFault (INVSTATE) before any
 * instruction decodes.  Program storage origins (the TPA base, the XIP
 * CCP entry) are naturally even, so this trampoline is where the ISA
 * adds the Thumb bit at transfer time.  If the target returns, control
 * returns to the caller (identical to an ordinary function-pointer call).
 * See kernel.h.
 *
 * The arch config compiles driver sources with -mthumb; be explicit here
 * so this file assembles correctly regardless of driver flags.
 */

.section .text, "ax"
.align 2
.syntax unified
.thumb
.global kjump
.thumb_func

kjump:
    adds r0, r0, #1     /* _entry address: Thumb bit (bit0) */
    bx   r0