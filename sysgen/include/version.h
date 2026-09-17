/*
 * sysgen/include/version.h — CP/M Neo version numbers
 *
 * Single source of truth for all component versions.
 * Sysgen stamps OS/KERN/CCP into sector 0; the kernel reads the
 * constants directly at runtime.  SDK_VER is tracked for future use.
 */

#ifndef SYSGEN_VERSION_H
#define SYSGEN_VERSION_H

#define OS_VER      0x0100 /* 1.0 — Release          */
#define KERN_VER    0x0100 /* 1.0 — Kernel ABI       */
#define CCP_VER     0x0100 /* 1.0 — CCP commands     */
#define SDK_VER     0x0100 /* 1.0 — User API         */
#define SYSGEN_VER  0x0100 /* 1.0 — Build tool       */

#endif /* SYSGEN_VERSION_H */