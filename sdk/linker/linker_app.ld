/* Plain (non-XIP) app/CCP linker script.
 *
 * The whole program lives in the TPA RAM region between the TPA base and
 * the kernel.  The shared section layout lives in sdk/linker/linker_sdk.ld;
 * this wrapper declares the memory region and binds the abstract placement
 * name TEXT that the body refers to.  __stack_size / __stack_top are owned
 * by the kernel link and arrive via --just-symbols; __tpa_base and
 * __kernel_base likewise. */

MEMORY {
    /* Length = available TPA RAM (from __tpa_base up to the kernel) */
    RAM (rwx) : ORIGIN = __tpa_base, LENGTH = __kernel_base - __tpa_base
}

REGION_ALIAS("TEXT", RAM)

INCLUDE sdk/linker/linker_sdk.ld