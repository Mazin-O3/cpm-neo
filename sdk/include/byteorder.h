/*
 * sdk/include/byteorder.h — Little-endian byte accessors
 *
 * Header-only static inline helpers shared by the kernel (on-disk / ELF
 * structures) and the sysgen host tool, so every consumer gets them from
 * one place without linking an SDK object.  Unreferenced in a TU they
 * compile to nothing.
 */

#ifndef SDK_BYTEORDER_H
#define SDK_BYTEORDER_H

#include <stdint.h>

/* get/put a u16 at an arbitrary byte offset (little-endian). */
static inline uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

#endif /* SDK_BYTEORDER_H */