#define _POSIX_C_SOURCE 200809L
#include "pixel_convert.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Original byte loop, retained as an independent format oracle and benchmark. */
__attribute__((noinline)) static void reference(uint8_t *dst, size_t stride,
        const uint8_t *src, unsigned width, unsigned height) {
    for (uint32_t y=0;y<height;y++) {
        uint8_t *d=dst+(size_t)y*stride;
        const uint8_t *s=src+(size_t)y*width*4;
        for(uint32_t x=0;x<width;x++) {
            d[x*4]=s[x*4+2]; d[x*4+1]=s[x*4+1];
            d[x*4+2]=s[x*4]; d[x*4+3]=s[x*4+3];
        }
    }
}
static uint64_t ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
int main(int argc, char **argv) {
    unsigned checks=0;
    for (unsigned w=0;w<=97;w++) for(unsigned h=1;h<=9;h++) for(unsigned pad=0;pad<=11;pad++) {
        size_t stride=(size_t)w*4+pad, bytes=stride*h, source_bytes=(size_t)w*h*4;
        uint8_t *a=malloc(bytes+64),*b=malloc(bytes+64),*source=malloc(source_bytes+64);
        assert(a&&b&&source);
        for(size_t i=0;i<source_bytes+64;i++) source[i]=(uint8_t)(i*37+w+h);
        memset(a,0xa5,bytes+64); memset(b,0xa5,bytes+64);
        // Deliberately unaligned starts, tails, alpha and row padding.
        reference(a+3,stride,source+1,w,h);
        lab_bgra_to_rgba(b+3,stride,source+1,(size_t)w*4,w,h);
        assert(!memcmp(a,b,bytes+64)); checks++;
        free(a);free(b);free(source);
    }
    printf("PIXEL_CONVERT_TEST checks=%u failures=0 backend=%s\n",checks,
#ifdef __ARM_NEON
        "neon"
#else
        "scalar"
#endif
    );
    if(argc>1 && !strcmp(argv[1],"--benchmark")) {
        const unsigned w=1024,h=768; size_t bytes=(size_t)w*h*4;
        uint8_t *s=malloc(bytes),*a=malloc(bytes),*b=malloc(bytes);assert(s&&a&&b);
        for(size_t i=0;i<bytes;i++)s[i]=(uint8_t)(i*37);
        for(unsigned round=0;round<12;round++) {
            uint64_t old=0,simd=0;
            for(unsigned order=0;order<2;order++) {
                int use_simd=(order+(round&1))&1;
                uint64_t began=ns();
                for(unsigned i=0;i<100;i++) {
                    if(use_simd)lab_bgra_to_rgba(b,w*4,s,w*4,w,h);else reference(a,w*4,s,w,h);
                    // Make every written frame externally observable to compiler.
                    __asm__ volatile("" : : "r"(use_simd?b:a) : "memory");
                }
                if(use_simd)simd=ns()-began;else old=ns()-began;
            }
            assert(!memcmp(a,b,bytes));
            printf("PIXEL_BENCH round=%u width=%u height=%u frames=100 reference_ns=%llu vector_ns=%llu scope=conversion_only\n",
                round,w,h,(unsigned long long)old,(unsigned long long)simd);
        }
        free(s);free(a);free(b);
    }
}
