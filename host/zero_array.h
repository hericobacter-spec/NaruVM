#ifndef HENGE_ZERO_ARRAY_H
#define HENGE_ZERO_ARRAY_H
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#if defined(__ANDROID__) || defined(HV_TEST_ZERO_ARRAY)
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Persistent large tables need zero pages, not an eager allocator memset.
 * Keep inaccessible pages at both ends and a checked 32-byte tail immediately
 * after the logical array, even when its length is not a page multiple. */
static inline void *hv_zero_array(size_t count, size_t element) {
    if (element && count>SIZE_MAX/element) { errno=ENOMEM; return NULL; }
    size_t bytes=count*element;
    if (bytes<(1u<<20)) return calloc(count,element);
    long page=sysconf(_SC_PAGESIZE);
    if (page<=0 || bytes>SIZE_MAX-32-3*(size_t)page) { errno=ENOMEM; return NULL; }
    size_t data=(bytes+32+(size_t)page-1)/(size_t)page*(size_t)page;
    char *base=mmap(NULL,data+2*(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if (base==MAP_FAILED) return NULL;
    char *at=base+page;
    if (mprotect(at,data,PROT_READ|PROT_WRITE)) {
        int saved=errno; munmap(base,data+2*(size_t)page); errno=saved; return NULL;
    }
    memset(at+bytes,0xa7,32);
    return at;
}
static inline void hv_zero_array_free(void *at, size_t count, size_t element) {
    if (!at) return;
    size_t bytes=count*element;
    if (bytes<(1u<<20)) { free(at); return; }
    for (unsigned i=0;i<32;i++) if (((unsigned char *)at)[bytes+i]!=0xa7) {
        fprintf(stderr,"CPU_TABLE_TAIL_CORRUPTED bytes=%zu offset=%u\n",bytes,i); abort();
    }
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    size_t data=(bytes+32+page-1)/page*page;
    munmap((char *)at-page,data+2*page);
}
#else
static inline void *hv_zero_array(size_t count,size_t element) { return calloc(count,element); }
static inline void hv_zero_array_free(void *at,size_t count,size_t element) { (void)count;(void)element;free(at); }
#endif
#endif
