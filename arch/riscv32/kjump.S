/* arch/riscv32/kjump.S
 * CP/M Neo — Transfer control to a program loaded at |a0|.
 *
 * RISC-V has no instruction-set state bit, so the target is branched
 * to directly.  If the target returns, control returns to the caller
 * (identical to an ordinary function-pointer call).  See kernel.h.
 */

.section .text
.global kjump
kjump:
	jr a0