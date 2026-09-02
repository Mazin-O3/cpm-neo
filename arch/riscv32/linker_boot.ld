ENTRY(_boot_start)

/* __boot_base and __boot_size (boot code region) are arch constants from
 * arch/<isa>/config.sh.  __ram_base is the platform RAM base, and
 * __boot_ram_size is the arch boot runtime RAM budget.  Boot code and boot
 * runtime RAM are disjoint regions, so code may live in flash/ROM while the
 * scratch buffer, stack, and bios .bss run from writable RAM. */
MEMORY {
    BOOT (rx) : ORIGIN = __boot_base,               LENGTH = __boot_size
    BRAM (rw) : ORIGIN = __ram_base + __boot_size,  LENGTH = __boot_ram_size
}

SECTIONS {
    .boot : {
        *(.boot)
        *(.text .text.*)
        . = ALIGN(4);
        _boot_end = .;
    } > BOOT

    /* Writable boot data (e.g. bios.c statics) lives in boot runtime RAM,
     * never in the code region. */
    .sbss (NOLOAD) : {
        *(.sbss .sbss.*)
        *(.scommon)
        . = ALIGN(4);
        _boot_bss_end = .;
    } > BRAM

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
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
