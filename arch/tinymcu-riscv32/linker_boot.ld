ENTRY(_boot_start)

TINYMCU_ROM_BASE     = 0x00000000;
TINYMCU_RAM_BASE     = 0x02000000;
TINYMCU_BOOT_RAM_SIZE = 0x400;  /* reserved window for scratch+stack below */

MEMORY {
    ROM (rx) : ORIGIN = TINYMCU_ROM_BASE, LENGTH = 32K
}

SECTIONS {
    .boot TINYMCU_ROM_BASE : {
        *(.boot)
        *(.text .text.*)
        . = ALIGN(4);
        _boot_end = .;
    } > ROM

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}

/* Volatile boot RAM at the base of real SRAM: a 512-byte sector-0 scratch
 * buffer, then a 128-byte stack just above it. Only used while booting.
 * The region is reused by the TPA once the kernel runs, same intent as
 * the original vemu layout, just anchored to RAM_BASE instead of "right
 * after the code" (which on this platform would be inside read-only ROM). */
__boot_scratch   = TINYMCU_RAM_BASE;
__boot_stack_top = __boot_scratch + 0x280;

ASSERT(__boot_stack_top <= TINYMCU_RAM_BASE + TINYMCU_BOOT_RAM_SIZE,
       "ERROR: boot scratch/stack overflow the reserved boot RAM window")
