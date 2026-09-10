ENTRY(_entry)

/* Shared SDK program layout — used by user .com apps (RAM) and the XIP CCP.
 *
 * The wrappers declare the concrete memory regions and bind the abstract
 * placement names used here:
 *   TEXT — region that holds .text (code + rodata); linker_app.ld maps it
 *          to RAM, linker_ccp_xip.ld to XIP_REGION.
 *   RAM  — region that holds writable .data/.bss and the stack; both
 *          wrappers already name it RAM.
 * The unified `AT(ADDR(.text) + SIZEOF(.text))` gives .data an explicit
 * load address; in the single-region (plain) build that LMA equals the
 * VMA — the same result as the old implicit placement.
 *
 * Configuration symbols arrive via --just-symbols from the kernel link
 * (__stack_size / __stack_top / __tpa_base / __kernel_base) except
 * __tpa_len / __kernel_xip_end, which the XIP build passes as --defsym. */

SECTIONS {
    .text : {
        . = ALIGN(4);
        _text_start = .;
        *(.text.entry)
        *(.text .text.*)
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

    /* Reserve the stack at the TOP of TPA RAM so its address is
     * independent of this program's size and the exec-time load can
     * never reach it.  BSS/Data can never grow upward into it (ASSERT
     * below).  Symbols are absolute (top-level assignments), not tied
     * to any section's location counter.
     */
    _stack_bottom = __stack_top - __stack_size;
    _stack_top    = __stack_top;

    ASSERT(ABSOLUTE(_bss_end) <= _stack_bottom, "ERROR: TPA code/BSS reaches stack")

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}