/* Plain (non-XIP) kernel linker script.
 *
 * Code, writable .data/.bss and the shared stack all live in one KRAM
 * region packed below the top of usable RAM.  The two-pass build derives
 * __KERN_START; __io_base, __ram_top and __tpa_base arrive via --defsym.
 *
 * The shared section layout lives in linker_kernel_common.ld; this wrapper
 * only declares the concrete memory region and binds the abstract
 * placement names TEXT/RAM that the body refers to. */

MEMORY {
    KRAM    (rwx) : ORIGIN = __KERN_START, LENGTH = __ram_top - __KERN_START
}

REGION_ALIAS("TEXT", KRAM)
REGION_ALIAS("RAM",  KRAM)

INCLUDE core/kernel/linker_kernel_common.ld