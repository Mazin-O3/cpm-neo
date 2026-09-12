/*
 * bootloader/boot.S
 * CP/M Neo 32-bit ARM Bootloader
 *
 * Cortex-M (ARMv7-M).
 *
 * Runs from flash at __boot_base (0x08000000).
 * Initializes hardware, reads disk sector 0, then branches to
 * the kernel.
 *
 * Every Cortex-M branch target has the Thumb bit set.
 */

#include "disk_format.h"

.extern bios_init
.extern bios_read
.extern bios_conout

.section .boot, "ax"
.align 2

/* ── Cortex-M vector table ────────────────────────────────── */

.global _boot_start
_boot_start:
    .word __boot_stack_top
    .word _reset_handler + 1

    .word _boot_halt + 1       /* NMI */
    .word _boot_halt + 1       /* HardFault */
    .word _boot_halt + 1       /* MemManage */
    .word _boot_halt + 1       /* BusFault */
    .word _boot_halt + 1       /* UsageFault */

    .word 0
    .word 0
    .word 0
    .word 0

    .word _boot_halt + 1       /* SVCall */

    .word 0
    .word 0

    .word _boot_halt + 1       /* PendSV */
    .word _boot_halt + 1       /* SysTick */

.align 2
.syntax unified
.thumb
.thumb_func

_reset_handler:

    /* Boot runtime RAM. */
    ldr sp, =__boot_stack_top
    ldr r8, =__boot_scratch

    /* Set vector table to the bootloader. */
    ldr r0, =0xE000ED08
    ldr r1, =__boot_base
    str r1, [r0]

    /*
     * Initialize hardware.
     */
    bl bios_init
    cmp r0, #0
    blt .Lspin

    /* Print boot banner. */
    ldr r0, =msg_title
    bl print_str

    /* ── Load sector 0 ─────────────────────────────────────── */

    mov r0, #0
    mov r1, r8
    bl bios_read
    cmp r0, #0
    blt err_disk

    /* Verify 0xAA55 boot signature. */
    ldrh r0, [r8, #S0_SIG]
    ldr r1, =BOOT_SIG
    cmp r0, r1
    bne err_disk

    ldr r0, =msg_disk_ok
    bl print_str

    /* ── XIP path ──────────────────────────────────────────── */

    ldrb r0, [r8, #S0_XIP]
    cbz r0, .Lno_xip

    /*
     * Kernel .text already resides in the XIP region.
     */
    ldr r0, =__xip_base
    cmp r0, #0
    beq err_disk

    ldr r0, =msg_loading
    bl print_str

    ldr r0, =msg_kernel_ok
    bl print_str

    /*
     * Cortex-M requires bit 0 set on the branch target.
     */
    ldr r0, =__xip_base
    orr r0, r0, #1
    bx r0

/* ── RAM-loaded path ──────────────────────────────────────── */

.Lno_xip:

    /*
     * Load 32-bit kernel target address.
     *
     * Keep this address in r1 for the final branch.
     */
    ldrh r1, [r8, #S0_KERN_LOAD]
    ldrh r0, [r8, #(S0_KERN_LOAD + 2)]
    lsl r0, r0, #16
    orr r1, r1, r0

    /*
     * r2 = number of kernel sectors.
     * r5 = first kernel sector.
     */
    ldrh r2, [r8, #S0_KERN_SECTORS]
    ldrh r5, [r8, #S0_KERN_SEC]

    cbz r2, err_disk

    /*
     * Validate memory bounds.
     */
    lsl r3, r2, #9
    add r3, r1, r3

    ldr r4, =__boot_stack_top
    cmp r1, r4
    blo err_disk

    ldr r4, =__ram_top
    cmp r3, r4
    bhi err_disk

    ldr r0, =msg_loading
    bl print_str

    /*
     * Load kernel:
     *
     *   r4 = destination
     *   r5 = disk sector
     *   r6 = remaining sectors
     */
    mov r4, r1
    mov r6, r2

.Lload_kern:

    mov r0, r5
    mov r1, r4
    bl bios_read
    cmp r0, #0
    blt err_disk

    movw r0, #DISK_SECTOR_SIZE
    add r4, r4, r0

    adds r5, r5, #1
    subs r6, r6, #1
    bne .Lload_kern

    /*
     * Verify 16-bit kernel magic at end of loaded buffer.
     *
     * r1 is restored to the kernel load address after the check.
     */
    lsl r3, r2, #9
    add r3, r1, r3

    ldrh r0, [r3, #-4]

    ldr r4, =BOOT_MAGIC
    cmp r0, r4
    bne err_kernel

    ldr r0, =msg_kernel_ok
    bl print_str

    /*
     * Jump to kernel.
     *
     * r1 still contains the kernel load address.
     * Set Thumb bit before BX.
     */
    orr r1, r1, #1
    bx r1

/* ── Print string ─────────────────────────────────────────── */

.thumb_func
print_str:

    push {r4, lr}
    mov r4, r0

.Lstr_loop:

    ldrb r0, [r4], #1
    cbz r0, .Ldone_str

    bl bios_conout
    b .Lstr_loop

.Ldone_str:

    pop {r4, pc}

/* ── Error paths ───────────────────────────────────────────── */

.thumb_func
_boot_halt:
    b _boot_halt

.thumb_func
err_kernel:
    ldr r0, =msg_err_kernel
    b .Lhalt

.thumb_func
err_disk:
    ldr r0, =msg_err_disk

.thumb_func
.Lhalt:
    bl print_str

.Lspin:
    b .Lspin

/* ── Strings ──────────────────────────────────────────────── */

msg_title:
    .asciz "CP/M Neo ARM Cortex-M Bootloader\n\n"

msg_disk_ok:
    .asciz "- Disk OK\n"

msg_loading:
    .asciz "- Loading Kernel\n"

msg_kernel_ok:
    .asciz "- Kernel OK\n"

msg_err_disk:
    .asciz "- DISK ERR\n"

msg_err_kernel:
    .asciz "- KERNEL ERR\n"

/* Boot sector signature for linker. */
.hword 0xAA55

