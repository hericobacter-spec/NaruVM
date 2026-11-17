#ifndef NARUVM_PIXEL_CONVERT_H
#define NARUVM_PIXEL_CONVERT_H
#include <stddef.h>
#include <stdint.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* Non-overlapping BGRA/RGBA buffers. Preserve alpha and destination row padding. */
static inline void lab_bgra_to_rgba(uint8_t *restrict dst, size_t dst_stride,
        const uint8_t *restrict src, size_t src_stride, unsigned width, unsigned height) {
    for (unsigned y=0; y<height; y++) {
        uint8_t *d=dst+(size_t)y*dst_stride;
        const uint8_t *s=src+(size_t)y*src_stride;
        unsigned x=0;
#if defined(__ARM_NEON)
        for (; width-x>=16; x+=16) {
            uint8x16x4_t p=vld4q_u8(s+(size_t)x*4);
            uint8x16_t b=p.val[0]; p.val[0]=p.val[2]; p.val[2]=b;
            vst4q_u8(d+(size_t)x*4,p);
        }
#endif
        for (; x<width; x++) {
            d[x*4]=s[x*4+2]; d[x*4+1]=s[x*4+1];
            d[x*4+2]=s[x*4]; d[x*4+3]=s[x*4+3];
        }
    }
}
#endif
