ENTRY(_entry)

/* Shared kernel section body, included by the plain and XIP kernel
 * linker scripts.  The wrappers define the concrete memory regions and
 * bind the abstract placement names used here:
 *   TEXT — region that holds .text (code + rodata); the XIP wrapper maps
 *          it to XIP_REGION, the plain wrapper to KRAM.
 *   RAM  — region that holds writable .data/.bss and the shared stack;
 *          both wrappers map it to KRAM.
 * The unified `AT(ADDR(.text) + SIZEOF(.text))` gives .data an explicit
 * load address; in the single-region (plain) build that LMA equals the
 * VMA — the same result as the old implicit placement.
 *
 * __KERN_START, __io_base, __ram_top and __tpa_base are supplied via
 * --defsym= in the two-pass build.  __io_base is the MMIO window base,
 * __ram_top is the top of usable RAM (min(RAM_BASE + RAM_SIZE, IO_BASE))
 * and __tpa_base is the CP/M TPA load address (RAM_BASE + 0x100).
 */

__kernel_base = ORIGIN(RAM);

SECTIONS {
    .text : {
        . = ALIGN(4);
        _text_start = .;
        *(.text.entry)
        *(.text .text.*)
        /* System calls are the kernel's user-facing ABI; apps and the CCP
         * call them via --just-symbols.  Keep them as GC roots so a link
         * without a jump table still exports the full syscall surface. */
        KEEP(*(.text.sys_*))
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        _text_end = .;
    } > TEXT

    .data : AT(ADDR(.text) + SIZEOF(.text)) {
        . = ALIGN(4);
        _data_start = .;

        *(.sdata .sdata.*)
        *(.data .data.*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM

    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    /* End of the kernel image (aligned). */
    _kend = ALIGN(16);

    __kernel_total = ABSOLUTE(_kend) - ORIGIN(RAM);

    /* The kernel, the CCP, and user programs share a single stack at the
     * top of the TPA (the top slice of the TPA region, at __kernel_base).
     * There is no dedicated kernel stack: syscalls are plain direct calls
     * that run on the caller's stack, and there are no interrupt or trap
     * handlers, so a private kernel stack is only ever needed during the
     * initial boot — which runs on this same stack (growing down into the
     * then-empty TPA).  These symbols are owned here, in the kernel
     * link, and flow to the CCP/apps via --just-symbols. */
    __stack_size = 0x1000;
    __stack_top  = ORIGIN(RAM);
    _stack_top   = __stack_top;

    ASSERT(_kend <= __ram_top, "ERROR: kernel image reaches top of usable RAM")
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}