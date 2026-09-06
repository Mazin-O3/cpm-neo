/*
 * bootloader/boot.S
 * CP/M Neo 32-bit RISC-V Bootloader
 */

#include "s0_layout.h"

.equ DISK_MAGIC,      0x4350
.equ BOOT_SIG,        0xAA55
.equ BOOT_MAGIC,      DISK_MAGIC
.equ SECTOR_SIZE,     512

.extern bios_init
.extern bios_read
.extern bios_conout

.section .boot, "ax"
.global _boot_start
.align 2

_boot_start:
    /* Set up boot runtime RAM (scratch buffer and stack) */
    la   sp, __boot_stack_top
    la   s7, __boot_scratch

    /* Initialize hardware. Silent halt on failure (no console yet). */
    jal  ra, bios_init
    bltz a0, .Lspin

    /* Print boot banner */
    la   a0, msg_title
    jal  ra, print_str

    /* Load sector 0 into scratch buffer */
    li   a0, 0
    mv   a1, s7
    jal  ra, bios_read
    bltz a0, err_disk

    /* Verify 0xAA55 boot signature */
    lhu  t0, S0_SIG(s7)
    li   t1, BOOT_SIG
    bne  t0, t1, err_disk

    la   a0, msg_disk_ok
    jal  ra, print_str

    /* Check Execute-In-Place (XIP) flag */
    lbu  t0, S0_XIP(s7)
    beqz t0, .Lno_xip

    /* XIP: the kernel .text already resides in the XIP region at
     * __xip_base.  Jump straight to its XIP entry:
     * crt0 is position-independent and bootstraps from the XIP region
     * (stack, gp, .data copy from the XIP region, .bss zero) before
     * reaching C code.  The BOOT_MAGIC trailer check is skipped too —
     * that magic marks the end of the RAM-loaded image and is
     * meaningless in place. */
    lui  t0, %hi(__xip_base)
    addi t0, t0, %lo(__xip_base)
    beqz t0, err_disk          /* Safety: no XIP base wired into boot */
    la   a0, msg_loading
    jal  ra, print_str
    la   a0, msg_kernel_ok
    jal  ra, print_str
    jalr zero, t0, 0           /* Take off to the XIP entry — no return */

.Lno_xip:

    /* Load 32-bit kernel target address safely (handling 2-byte alignment) */
    lhu  s1, S0_KERN_LOAD(s7)
    lhu  t0, (S0_KERN_LOAD + 2)(s7)
    slli t0, t0, 16
    or   s1, s1, t0            /* s1 = 32-bit load address */

    lhu  s2, S0_KERN_SECTORS(s7)
    lhu  s6, S0_KERN_SEC(s7)
    beqz s2, err_disk

    /* Validate memory bounds to protect bootloader RAM and physical RAM */
    slli t0, s2, 9             /* t0 = total kernel bytes (sectors * 512) */
    add  t1, s1, t0            /* t1 = end of loaded kernel */

    la   t2, __boot_stack_top
    bltu s1, t2, err_disk      /* Err: Start address clobbers boot RAM */

    la   t2, __ram_top
    bgtu t1, t2, err_disk      /* Err: End address exceeds usable RAM */

    la   a0, msg_loading
    jal  ra, print_str

    /* Load kernel binary */
    mv   s3, s1                /* Destination RAM address */
    mv   s4, s6                /* Starting sector */
    mv   s5, s2                /* Remaining sectors */

.Lload_kern:
    mv   a0, s4
    mv   a1, s3
    jal  ra, bios_read
    bltz a0, err_disk          /* Err: Bios read failure */
    
    addi s3, s3, SECTOR_SIZE
    addi s4, s4, 1
    addi s5, s5, -1
    bnez s5, .Lload_kern

    /* Verify 16-bit kernel magic at end of loaded buffer */
    slli t0, s2, 9             
    add  t0, s1, t0            
    lhu  t0, -4(t0)            /* Load magic halfword from end-4 */
    li   t1, BOOT_MAGIC
    bne  t0, t1, err_kernel

    la   a0, msg_kernel_ok
    jal  ra, print_str

    /* Jump to kernel */
    jalr zero, s1, 0

/* ── Helper Routines ─────────────────────────────────────── */

/* print_str: Prints null-terminated string pointed to by a0 */
print_str:
    addi sp, sp, -16           /* Maintain 16-byte stack alignment */
    sw   ra, 12(sp)
    sw   s0, 8(sp)
    mv   s0, a0                /* Preserve string pointer across calls */

.Lstr_loop:
    lbu  a0, 0(s0)
    beqz a0, .Ldone_str
    addi s0, s0, 1
    jal  ra, bios_conout
    j    .Lstr_loop

.Ldone_str:
    lw   s0, 8(sp)
    lw   ra, 12(sp)
    addi sp, sp, 16
    ret

/* ── Error paths ─────────────────────────────────────────── */
err_kernel:
    la   a0, msg_err_kernel
    j    .Lhalt
err_disk:
    la   a0, msg_err_disk
.Lhalt:
    jal  ra, print_str
.Lspin:
    j    .Lspin

/* ── Strings ─────────────────────────────────────────────── */
msg_title:      .asciz "CP/M Neo RISC-V Bootloader\n\n"
msg_disk_ok:    .asciz "- Disk OK\n"
msg_loading:    .asciz "- Loading Kernel\n"
msg_kernel_ok:  .asciz "- Kernel OK\n"
msg_err_disk:   .asciz "- DISK ERR\n"
msg_err_kernel: .asciz "- KERNEL ERR\n"

/* Boot sector signature for linker */
.hword 0xAA55