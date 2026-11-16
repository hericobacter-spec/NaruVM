/* Android port modifications: 2026-11-14 through 2026-11-16.
 * See NOTICE and docs/modifications.json; original copyright notices retained. */
#include "jit_region.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#else
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* The split shape on a host that can make it itself: plain RW memory, an
   alias, and the original turned R-X. Works for macOS binaries without the
   hardened runtime; on iOS the executable half has to come from a debugger. */
static int open_split(HvJitRegion *region, size_t bytes) {
#if !defined(__APPLE__)
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || bytes > SIZE_MAX - (size_t)page) return -1;
    size_t mapped = (bytes + (size_t)page - 1) & ~((size_t)page - 1);
    int fd = (int)syscall(SYS_memfd_create, "henge-jit", 1u /* MFD_CLOEXEC */);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)mapped) != 0) { close(fd); return -1; }
    void *rw = mmap(NULL, mapped, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    void *rx = mmap(NULL, mapped, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    int saved = errno;
    close(fd);
    if (rx == MAP_FAILED || rw == MAP_FAILED) {
        if (rx != MAP_FAILED) munmap(rx, mapped);
        if (rw != MAP_FAILED) munmap(rw, mapped);
        errno = saved;
        return -1;
    }
    if (hv_jit_region_attach(region, rx, rw, mapped) != 0) {
        munmap(rx, mapped); munmap(rw, mapped); return -1;
    }
    /* Capacity is the requested logical size, including the upstream 1 KB flush test. */
    region->capacity = bytes / sizeof(uint32_t);
    region->owned = 1;
    return 0;
#else
    void *rx = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (rx == MAP_FAILED) return -1;
    vm_address_t rw = 0;
    vm_prot_t cur, max;
    if (vm_remap(mach_task_self(), &rw, bytes, 0, VM_FLAGS_ANYWHERE, mach_task_self(), (vm_address_t)rx, FALSE, &cur, &max,
                 VM_INHERIT_NONE) != KERN_SUCCESS ||
        mprotect(rx, bytes, PROT_READ | PROT_EXEC) != 0) {
        munmap(rx, bytes);
        return -1;
    }
    if (hv_jit_region_attach(region, rx, (void *)rw, bytes) != 0) return -1;
    region->owned = 1;
    return 0;
#endif
}

int hv_jit_region_open(HvJitRegion *region, size_t bytes) {
    memset(region, 0, sizeof *region);
    if (getenv("HV_JIT_SPLIT")) return open_split(region, bytes);
#if TARGET_OS_OSX
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return -1;
    region->base = p;
    region->capacity = bytes / sizeof(uint32_t);
    region->owned = 1;
    return 0;
#else
    return open_split(region, bytes); /* works only where nothing enforces code signing (tests on a jailbroken device) */
#endif
}

int hv_jit_region_attach(HvJitRegion *region, void *rx, void *rw, size_t bytes) {
    memset(region, 0, sizeof *region);
    if (!rx || !rw || bytes < 4096) return -1;
    region->base = rx;
    region->rw_diff = (intptr_t)((uint8_t *)rw - (uint8_t *)rx);
    region->capacity = bytes / sizeof(uint32_t);
    return 0;
}

void hv_jit_region_close(HvJitRegion *region) {
    if (region->owned && region->base) {
        size_t bytes = region->capacity * sizeof(uint32_t);
        if (region->rw_diff) munmap((uint8_t *)region->base + region->rw_diff, bytes);
        munmap(region->base, bytes);
    }
    memset(region, 0, sizeof *region);
}

void hv_jit_write(HvJitRegion *region, uint32_t *start, const void *src, size_t bytes) {
    /* One instruction may be rewritten while other CPUs run it (chaining a branch).
       memcpy may copy a word in pieces, and a CPU fetching in between takes half of
       the old branch and half of the new one: a jump to nowhere. */
    if (bytes == 4) {
        uint32_t word;
        memcpy(&word, src, 4);
#if TARGET_OS_OSX
        if (!region->rw_diff) pthread_jit_write_protect_np(0);
#endif
        __atomic_store_n((uint32_t *)((uint8_t *)start + region->rw_diff), word, __ATOMIC_RELEASE);
        return;
    }
    if (region->rw_diff) {
        memcpy((uint8_t *)start + region->rw_diff, src, bytes);
        return;
    }
#if TARGET_OS_OSX
    pthread_jit_write_protect_np(0); /* MAP_JIT: writable on this thread only, until hv_jit_finish */
#endif
    memcpy(start, src, bytes);
}

#if defined(__ANDROID__) && defined(__aarch64__)
static void android_cache_lines(uintptr_t at, size_t words, size_t line, int instruction) {
    uintptr_t end = at + words * sizeof(uint32_t);
    if (!at || !words) return;
    for (at &= ~(uintptr_t)(line - 1); at < end; at += line) {
        if (instruction) __asm__ volatile("ic ivau, %0" : : "r"(at) : "memory");
        else __asm__ volatile("dc cvau, %0" : : "r"(at) : "memory");
    }
}

/* Split JIT: clean the alias that was written; invalidate the alias that runs.
   Both ranges share barriers. Readers retain their existing entry ISB protocol.
   https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/caches-self-modifying-code-working-with-threads */
static void android_finish_ranges(HvJitRegion *region, uint32_t *a, size_t na, uint32_t *b, size_t nb) {
    if ((!a || !na) && (!b || !nb)) return;
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    size_t dline = (size_t)4 << ((ctr >> 16) & 15), iline = (size_t)4 << (ctr & 15);
    if (!(ctr & (1ull << 28))) {
        if (a && na) android_cache_lines((uintptr_t)((uint8_t *)a + region->rw_diff), na, dline, 0);
        if (b && nb) android_cache_lines((uintptr_t)((uint8_t *)b + region->rw_diff), nb, dline, 0);
    }
    __asm__ volatile("dsb ish" : : : "memory");
    if (!(ctr & (1ull << 29))) {
        android_cache_lines((uintptr_t)a, na, iline, 1);
        android_cache_lines((uintptr_t)b, nb, iline, 1);
        __asm__ volatile("dsb ish" : : : "memory");
    }
    __asm__ volatile("isb" : : : "memory");
}
#endif

void hv_jit_finish(HvJitRegion *region, uint32_t *start, size_t words) {
    (void)region;
#if TARGET_OS_OSX
    if (!region->rw_diff) pthread_jit_write_protect_np(1);
#endif
    if (start && words) {
#if defined(__APPLE__)
        sys_icache_invalidate(start, words * sizeof(uint32_t));
#elif defined(__ANDROID__) && defined(__aarch64__)
        android_finish_ranges(region, start, words, NULL, 0);
#else
        /* Android CPUs may require a D-cache clean as well as an I-cache invalidate.
           Clean through the writable alias, then invalidate the executable alias. */
        char *rx = (char *)start, *rw = rx + region->rw_diff;
        __builtin___clear_cache(rw, rw + words * sizeof(uint32_t));
        __builtin___clear_cache(rx, rx + words * sizeof(uint32_t));
#endif
    }
}

/* As sys_icache_invalidate does it (libsystem_platform: DSB, IC IVAU per 64-byte line with a DSB after every 20 for
   the CPUs that need one, DSB, ISB), over both ranges. */
static void ic_lines(uint32_t *start, size_t words, int *n) {
    if (!start || !words) return;
    uintptr_t a = (uintptr_t)start & ~(uintptr_t)63, end = (uintptr_t)(start + words);
    for (; a < end; a += 64) {
        __asm__ volatile("ic ivau, %0" ::"r"(a) : "memory");
        if (++*n % 20 == 0) __asm__ volatile("dsb ish" ::: "memory");
    }
}

void hv_jit_finish2(HvJitRegion *region, uint32_t *a, size_t na, uint32_t *b, size_t nb) {
#if defined(__ANDROID__) && defined(__aarch64__)
    android_finish_ranges(region, a, na, b, nb);
#elif !defined(__APPLE__)
    hv_jit_finish(region, a, na);
    hv_jit_finish(region, b, nb);
#else
    (void)region;
#if TARGET_OS_OSX
    if (!region->rw_diff) pthread_jit_write_protect_np(1);
#endif
    int n = 0;
    __asm__ volatile("dsb ish" ::: "memory");
    ic_lines(a, na, &n);
    ic_lines(b, nb, &n);
    __asm__ volatile("dsb ish\n\tisb" ::: "memory");
#endif
}

void hv_jit_finish_branch(HvJitRegion *region, uint32_t *word) {
#if !defined(__APPLE__)
    hv_jit_finish(region, word, 1);
#else
    (void)region;
#if TARGET_OS_OSX
    if (!region->rw_diff) pthread_jit_write_protect_np(1);
#endif
    /* The store first (so a refetch finds the new word), then the line, with no wait for every CPU's cache. */
    __asm__ volatile("dsb ish\n\tic ivau, %0" ::"r"((uintptr_t)word & ~(uintptr_t)63) : "memory");
#endif
}

/* Any thread: an even number of words (blocks stay 8-byte aligned), taken atomically. */
uint32_t *hv_jit_reserve(HvJitRegion *region, size_t words) {
    words = (words + 1) & ~(size_t)1;
    size_t at = __atomic_fetch_add(&region->used, words, __ATOMIC_RELAXED);
    size_t cap = region->limit && region->limit < region->capacity ? region->limit : region->capacity;
    if (at + words > cap) return NULL; /* full: the flush resets used */
    return region->base + at;
}

void hv_jit_reset(HvJitRegion *region) {
    region->used = 0;
}
