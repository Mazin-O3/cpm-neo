/*
 * sysgen/include/version.h — CP/M Neo version numbers
 *
 * Single source of truth for all component versions.
 * Sysgen stamps OS/KERN/CCP into sector 0; the kernel reads the
 * constants directly at runtime.  SDK_VER is tracked for future use.
 */

#ifndef SYSGEN_VERSION_H
#define SYSGEN_VERSION_H

#define OS_VER      0x0001 /* 0.1 — Release          */
#define KERN_VER    0x0001 /* 0.1 — Kernel ABI       */
#define CCP_VER     0x0001 /* 0.1 — CCP commands     */
#define SDK_VER     0x0001 /* 0.1 — User API         */
#define SYSGEN_VER  0x0001 /* 0.1 — Build tool       */

#endif /* SYSGEN_VERSION_H */