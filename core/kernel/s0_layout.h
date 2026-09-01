#ifndef S0_LAYOUT_H
#define S0_LAYOUT_H

/* Sector-0 (S0) field offsets.  This is the single source of truth shared
 * by the bootloader (arch/<isa>/boot.S), the kernel, and sysgen so the
 * on-disk layout can never drift between the three consumers.  The header
 * contains only preprocessor defines so it is usable from C and from GAS
 * assembly (boot.S is preprocessed by the C compiler). */

#define S0_MAGIC        0x000        /* u16   — Must equal DISK_MAGIC  */
#define S0_DISK_VER     0x002        /* u16   — Disk format version    */
#define S0_DISK_SIZE_KB 0x004        /* u16   — Total disk KB          */
#define S0_KERN_LOAD    0x006        /* u32   — Kernel RAM load addr   */
#define S0_KERN_SIZE    0x00A        /* u32   — Kernel.bin raw bytes   */
#define S0_KERN_SECTORS 0x00E        /* u16   — Padded on-disk sectors */
#define S0_KERN_SEC     0x010        /* u16   — Kernel start sector   */
#define S0_OS_VER       0x012        /* u16   — OS version             */
#define S0_KERN_VER     0x014        /* u16   — Kernel version         */
#define S0_CCP_VER      0x016        /* u16   — CCP version            */
#define S0_KERN_SECS    0x018        /* u16   — Kernel disk reservation */
#define S0_CCP_SEC      0x01A        /* u16   — CCP raw binary start sector */
#define S0_CCP_SIZE     0x01C        /* u16   — CCP raw binary sector count */
#define S0_PLATFORM     0x01E        /* u8[8] — Platform name, NUL-padded */
#define S0_SIG          0x1FE        /* u16   — Must equal BOOT_SIG    */

#endif /* S0_LAYOUT_H */