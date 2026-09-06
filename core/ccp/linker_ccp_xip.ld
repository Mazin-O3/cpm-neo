/* XIP CCP linker script.
 *
 * Used only for the CCP when the disk is XIP-formatted.  Its code executes
 * in place from the XIP region, starting at __kernel_xip_end — the
 * sector-aligned end of the kernel's XIP footprint (.text + .data image).
 * sysgen places ccp.bin at the sector right after the kernel, so the runtime
 * address equals the link origin and the CCP runs at Δ=0.  Writable
 * .data/.bss and the stack live in the TPA RAM region below the kernel.
 * User .com files are always RAM-loaded and never link against this script.
 *
 * The shared section layout lives in sdk/linker/linker_sdk.ld; this wrapper
 * declares the memory regions, binds TEXT to the XIP region, and appends
 * the region-geometry check.
 *
 * Configuration Symbols:
 *   __kernel_xip_end — sector-aligned end of the kernel's XIP footprint;
 *                  the CCP is linked to begin executing here (from
 *                  kernel.elf, passed back as a --defsym constant by the
 *                  build so GNU ld folds it into the MEMORY geometry
 *                  reliably)
 *   __xip_top        — exclusive end of the XIP window
 *                  (XIP_BASE + XIP_SIZE, from the build as --defsym)
 *   __tpa_base       — CP/M TPA load base (via --just-symbols)
 *   __tpa_len        — TPA length, __kernel_base − __tpa_base (--defsym)
 *   __stack_size / __stack_top — owned by the kernel link */

/* Regions reference the kernel-derived symbols DIRECTLY: GNU ld will not
 * fold an intermediate script assignment (e.g. X = sym; ORIGIN = X) into
 * MEMORY geometry — it reports "invalid origin for memory region XIP_REGION"
 * and silently sets the origin to 0 — but it reliably folds plain constant
 * expressions over --defsym symbols, the same idiom the kernel scripts use. */

MEMORY {
    XIP_REGION (rx) : ORIGIN = __kernel_xip_end,
                      LENGTH = __xip_top - __kernel_xip_end
    RAM        (rwx) : ORIGIN = __tpa_base, LENGTH = __tpa_len
}

REGION_ALIAS("TEXT", XIP_REGION)

INCLUDE sdk/linker/linker_sdk.ld

ASSERT(__kernel_xip_end < __xip_top,
       "ERROR: kernel XIP footprint exceeds XIP region size")