/* cortex-m boot linker script
 * CP/M Neo ARM Cortex-M bootloader link
 *
 * __boot_base and __ram_base come from the platform (board-specific
 * reset vector origin and SRAM base).  __boot_size and __boot_ram_size
 * default to 4 KB and 2 KB respectively; a platform that needs a
 * different budget passes --defsym to override.
 *
 * Boot code and boot runtime RAM are disjoint regions: code lives in
 * read-only flash while the scratch buffer, stack, and bios .bss run
 * from writable SRAM.
 *
 * The ARM boot begins with a Cortex-M vector table: word[0] = initial
 * MSP (__boot_stack_top), word[1] = reset handler (Thumb bit set), the
 * rest fall through to a halt loop.  It is emitted as the first .boot
 * contents so bootloader.bin (--only-section=.boot) begins with it; the
 * hardware reset vector points straight at it.
 */
ENTRY(_boot_start)

/* Boot memory budget — override via --defsym if the platform needs
 * a different allocation. */
PROVIDE(__boot_size     = 0x1000);    /* 4 KB */
PROVIDE(__boot_ram_size = 0x800);     /* 2 KB */

MEMORY {
    BOOT (rx) : ORIGIN = __boot_base,               LENGTH = __boot_size
    BRAM (rw) : ORIGIN = __ram_base + __boot_size,  LENGTH = __boot_ram_size
}

SECTIONS {
    .boot : {
        *(.boot)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        _boot_end = .;
    } > BOOT

    /* Writable boot data (e.g. bios.c statics) lives in boot runtime RAM,
     * never in the code region.  Only zero-initialized boot statics are
     * used; any non-zero initializers would need a boot-time .data copy. */
    .data : {
        *(.data .data.*)
    } > BRAM

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _boot_bss_end = .;
    } > BRAM

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.ARM.exidx)
        *(.ARM.extab)
        *(.ARM.attributes)
    }
}

/* Boot runtime RAM: scratch buffer at the end of .bss, stack at the top of
 * the reservation (grows down, away from the scratch buffer). */
__boot_scratch   = _boot_bss_end;
__boot_stack_top = ORIGIN(BRAM) + LENGTH(BRAM);

ASSERT(_boot_bss_end <= ORIGIN(BRAM) + LENGTH(BRAM),
       "ERROR: boot .bss exceeds the boot runtime RAM region")
ASSERT(__boot_stack_top <= __ram_top,
       "ERROR: boot runtime RAM reaches top of usable RAM — image too large for this memory size")