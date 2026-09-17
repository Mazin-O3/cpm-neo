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

#include <stdint.h>
#include <stdio.h>

#define OS_VER      0x0100 /* 1.0 */
#define KERN_VER    0x0100 /* 1.0 */
#define CCP_VER     0x0100 /* 1.0 */
#define SDK_VER     0x0100 /* 1.0 */
#define SYSGEN_VER  0x0100 /* 1.0 */

static inline int ver_fmt(char *buf, size_t n, uint16_t ver)
{
    uint8_t major = ver >> 8;
    uint8_t minor = ver & 0xFF;

    if (minor == 0)
        return snprintf(buf, n, "%u.0", major);

    if (minor < 10)
        return snprintf(buf, n, "%u.0%u", major, minor);
        
    return snprintf(buf, n, "%u.%u", major, minor);
}

#endif /* SYSGEN_VERSION_H */