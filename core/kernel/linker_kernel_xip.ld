/* XIP kernel linker script.
 *
 * The kernel .text (code + rodata) executes in place from the XIP region.
 * Its entry lives at __kernel_xip_base = XIP_BASE + KERN_START_SEC*512 —
 * the XIP address of the first byte of kernel.bin on the disk, right past
 * the bootloader and volume-map sectors (the bootloader jumps there exactly).
 * Writable .data, .bss and the shared stack live in RAM near the top of
 * usable RAM (packed below __ram_top, exactly like the non-XIP build's RAM
 * layout).  __KERN_START is the RAM base for the kernel's writable state;
 * the TPA sits below it.
 *
 * The shared section layout lives in linker_kernel_common.ld; this wrapper
 * declares the memory regions, binds the abstract placement names TEXT/RAM
 * the body refers to, and appends the XIP-only geometry tail.
 *
 * Symbols supplied via --defsym= in the two-pass build:
 *   __KERN_START      — RAM base for kernel .data/.bss/stack
 *   __io_base         — MMIO window base
 *   __ram_top         — top of usable RAM
 *   __tpa_base        — CP/M TPA load address
 *   XIP_BASE          — XIP window base (set by the build; also a runtime symbol)
 *   __kernel_xip_base — kernel's XIP entry/placement address
 *
 * The XIP region has no configured size: LENGTH is a never-binding
 * placeholder (its only job is to satisfy GNU ld's MEMORY grammar).  The
 * kernel's real XIP footprint ends at __kernel_xip_end (below), and whether
 * that footprint fits the disk is enforced later by sysgen/mkdisk, not by a
 * link-time window.                                             */

MEMORY {
    XIP_REGION (rx) : ORIGIN = __kernel_xip_base, LENGTH = 0x80000000
    KRAM      (rwx) : ORIGIN = __KERN_START, LENGTH = __ram_top - __KERN_START
}

REGION_ALIAS("TEXT", XIP_REGION)
REGION_ALIAS("RAM",  KRAM)

INCLUDE core/kernel/linker_kernel_common.ld

/* End of the kernel's XIP footprint (.text + .data image), padded to a
 * whole disk sector so the on-disk kernel image is exactly
 * __kernel_xip_end − __kernel_xip_base bytes.  mkdisk places the CCP at
 * the sector right after the kernel, so a sector-aligned end makes the
 * CCP's link origin (this value, flowing to the CCP via --just-symbols)
 * equal its on-disk placement — the CCP then runs in place at Δ=0.
 * User .com files are always RAM-loaded and never consume this region. */
__kernel_xip_end = ALIGN(_text_end + SIZEOF(.data), 512);