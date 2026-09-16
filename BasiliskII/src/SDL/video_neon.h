#ifndef VIDEO_NEON_H
#define VIDEO_NEON_H

#include <stddef.h>
#include <string.h>

#ifdef __aarch64__
#include <arm_neon.h>

static inline bool neon_memcmp_differs(const void *a, const void *b, size_t len)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    size_t i = 0;

    for (; i + 64 <= len; i += 64) {
        uint8x16_t eq0 = vceqq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i));
        uint8x16_t eq1 = vceqq_u8(vld1q_u8(pa + i + 16), vld1q_u8(pb + i + 16));
        uint8x16_t eq2 = vceqq_u8(vld1q_u8(pa + i + 32), vld1q_u8(pb + i + 32));
        uint8x16_t eq3 = vceqq_u8(vld1q_u8(pa + i + 48), vld1q_u8(pb + i + 48));
        if (vminvq_u8(vandq_u8(vandq_u8(eq0, eq1), vandq_u8(eq2, eq3))) == 0)
            return true;
    }

    for (; i + 16 <= len; i += 16) {
        if (vminvq_u8(vceqq_u8(vld1q_u8(pa + i), vld1q_u8(pb + i))) == 0)
            return true;
    }

    for (; i < len; i++) {
        if (pa[i] != pb[i])
            return true;
    }
    return false;
}

#else
static inline bool neon_memcmp_differs(const void *a, const void *b, size_t len)
{
    return memcmp(a, b, len) != 0;
}
#endif

#endif
