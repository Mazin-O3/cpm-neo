/*
 * sysgen/include/version.h — CP/M Neo version numbers
 *
 * Version scheme: 0xMMNN
 *   MM = major
 *   NN = minor: feature (tens) + patch (ones)
 *
 *   0x0100 = 1.0  - major
 *   0x0101 = 1.01 - patch fix
 *   0x0110 = 1.1  - new feature
 *   0x0141 = 1.41 - feature 4, patch 1
 *   0x0200 = 2.0  - next major
 */

#ifndef SYSGEN_VERSION_H
#define SYSGEN_VERSION_H

#define OS_VER      0x0100 /* 1.0 — Release          */
#define KERN_VER    0x0100 /* 1.0 — Kernel ABI       */
#define CCP_VER     0x0100 /* 1.0 — CCP commands     */
#define SDK_VER     0x0100 /* 1.0 — User API         */
#define SYSGEN_VER  0x0100 /* 1.0 — Build tool       */

#endif /* SYSGEN_VERSION_H */