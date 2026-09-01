/*
 * bootloader/boot.S
 * CP/M Neo 32-bit RISC-V Bootloader
 */

/* S0 (sector-0) field offsets come from core/kernel/s0_layout.h — the single
 * source of truth shared with the kernel and sysgen.  boot.S is compiled via
 * the C compiler, so the preprocessor substitutes the values below. */

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
    /* Volatile boot RAM lives as low as possible, right after the boot code:
     * the 512-byte sector-0 scratch buffer (__boot_scratch) and a 128-byte
     * stack just above it (__boot_stack_top), both from linker_boot.ld. */
    la   sp, __boot_stack_top
    la   s7, __boot_scratch

    /* Banner */
    la   t0, msg_title
    jal  ra, print_str

    /* Initialize hardware (BIOS) — required to read the kernel into RAM */
    jal  ra, bios_init
    bltz a0, err_bios

    la   t0, msg_bios_ok
    jal  ra, print_str

    /* Load sector 0 → scratch buffer */
    li   a0, 0
    mv   a1, s7
    jal  ra, bios_read
    bltz a0, err_disk

    /* Verify 0xAA55 signature */
    li   t0, S0_SIG
    add  t0, s7, t0
    lhu  t0, 0(t0)
    li   t1, BOOT_SIG
    bne  t0, t1, err_disk

    la   t0, msg_disk_ok
    jal  ra, print_str

    /* Load kernel target address safely (handling 2-byte alignment at 0x006) */
    lhu  s1, S0_KERN_LOAD(s7)
    lhu  t0, (S0_KERN_LOAD + 2)(s7)
    slli t0, t0, 16
    or   s1, s1, t0            /* s1 = 32-bit load address */

    lhu  s2, S0_KERN_SECTORS(s7)
    lhu  s6, S0_KERN_SEC(s7)
    beqz s2, err_disk

    la   t0, msg_loading
    jal  ra, print_str

    /* Load kernel binary — use saved registers across bios_read calls */
    mv   s3, s1                /* Target RAM destination */
    mv   s4, s6                /* Start sector */
    mv   s5, s2                /* Sector count remaining */

.Lload_kern:
    mv   a0, s4
    mv   a1, s3
    jal  ra, bios_read
    bltz a0, err_disk      /* bios_read: negative return = read failure */
    addi s3, s3, SECTOR_SIZE
    addi s4, s4, 1
    addi s5, s5, -1
    bnez s5, .Lload_kern

    /* Verify 16-bit kernel magic at end of loaded buffer */
    slli t0, s2, 9             /* total bytes loaded */
    add  t0, s1, t0            /* end pointer */
    lhu  t0, -4(t0)            /* load 16-bit halfword magic */
    li   t1, BOOT_MAGIC
    bne  t0, t1, err_kernel

    la   t0, msg_kernel_ok
    jal  ra, print_str

    /* Jump to kernel */
    jalr zero, s1, 0

/* ── Helper Routines ─────────────────────────────────────── */

print_str:
    addi sp, sp, -8
    sw   ra, 4(sp)
    sw   s0, 0(sp)
    mv   s0, t0                /* Use s0 to hold string pointer */

.Lstr_loop:
    lbu  a0, 0(s0)
    beqz a0, .Ldone_str
    addi s0, s0, 1
    jal  ra, bios_conout
    j    .Lstr_loop

.Ldone_str:
    lw   s0, 0(sp)
    lw   ra, 4(sp)
    addi sp, sp, 8
    ret

/* ── Error paths ─────────────────────────────────────────── */
err_kernel:
    la   t0, msg_err_kernel
    j    .Lhalt
err_disk:
    la   t0, msg_err_disk
    j    .Lhalt
err_bios:
    la   t0, msg_err_bios
    j    .Lhalt
.Lhalt:
    jal  ra, print_str
.Lspin:
    j    .Lspin

/* ── Strings ─────────────────────────────────────────────── */
msg_title:      .asciz "CP/M Neo RISC-V Bootloader\n\n"
msg_bios_ok:    .asciz "- BIOS OK\n"
msg_disk_ok:    .asciz "- Disk OK\n"
msg_loading:    .asciz "- Loading Kernel\n"
msg_kernel_ok:  .asciz "- Kernel OK\n"
msg_err_bios:   .asciz "- BIOS ERR\n"
msg_err_disk:   .asciz "- DISK ERR\n"
msg_err_kernel: .asciz "- KERNEL ERR\n"

.hword 0xAA55