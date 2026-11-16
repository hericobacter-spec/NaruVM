#define _GNU_SOURCE
/* Authored dual-alias instruction publication checks; no VM or guest disk. */
#include "jit_region.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static HvJitRegion jit;
static atomic_uint request, ack[2], failures;
static atomic_uint cpu_mask[3];
static uint32_t *hot, *cold;
static uint64_t ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static void body(uint32_t *p, unsigned value) {
    uint32_t code[] = {0x52800000u | ((value & 65535u) << 5), 0xd65f03c0u};
    hv_jit_write(&jit, p, code, sizeof code);
}
static unsigned call(uint32_t *p) {
    __asm__ volatile("isb" : : : "memory");
    return ((unsigned (*)(void))p)();
}
static void *reader(void *arg) {
    unsigned index=(unsigned)(uintptr_t)arg, last=0;
    for (;;) {
        unsigned n=atomic_load_explicit(&request,memory_order_acquire);
        if (n==last) { __asm__ volatile("yield"); continue; }
        if (n==65536) return NULL;
        int core=sched_getcpu();
        if (core>=0 && core<32) atomic_fetch_or(&cpu_mask[index],1u<<core);
        if (call(hot)!=n || call(cold)!=(n^0x55aa)) atomic_fetch_add(&failures,1);
        last=n; atomic_store_explicit(&ack[index],n,memory_order_release);
    }
}
static void baseline(uint32_t *a, size_t na, uint32_t *b, size_t nb) {
    uint32_t *ranges[]={a,b}; size_t sizes[]={na,nb};
    for (int i=0;i<2;i++) {
        char *rx=(char *)ranges[i], *rw=rx+jit.rw_diff;
        __builtin___clear_cache(rw,rw+sizes[i]*4);
        __builtin___clear_cache(rx,rx+sizes[i]*4);
    }
}
int main(void) {
    alarm(30); setvbuf(stdout,NULL,_IONBF,0);
    if (hv_jit_region_open(&jit,65536)) { perror("jit open"); return 1; }
    if (!jit.rw_diff) { puts("Split aliases required"); return 1; }
    unsigned checks=0;
    size_t offsets[]={0,60,4092,16380,65528};
    for (unsigned j=0;j<sizeof offsets/sizeof *offsets;j++) {
        uint32_t *p=(uint32_t *)((char *)jit.base+offsets[j]);
        for (unsigned i=1;i<=1024;i++) {
            body(p,i); hv_jit_finish(&jit,p,2);
            if (call(p)!=i) atomic_fetch_add(&failures,1);
        }
        checks++;
    }
    hot=jit.base+32; cold=jit.base+4096;
    for (unsigned i=1;i<=1024;i++) {
        body(hot,i); body(cold,i^0x55aa);
        hv_jit_finish2(&jit,hot,2,cold,2);
        if (call(hot)!=i || call(cold)!=(i^0x55aa)) atomic_fetch_add(&failures,1);
    }
    checks++;
    uint32_t *branch=jit.base+64;
    for (unsigned i=0;i<4096;i++) {
        uint32_t *target=i&1 ? hot : cold;
        uint32_t word=0x14000000u|((uint32_t)(target-branch)&0x3ffffffu);
        hv_jit_write(&jit,branch,&word,4); hv_jit_finish_branch(&jit,branch);
        if (call(branch)!=(i&1 ? 1024u : (1024u^0x55aa))) atomic_fetch_add(&failures,1);
    }
    checks++;
    pthread_t threads[2];
    for (unsigned i=0;i<2;i++) if (pthread_create(&threads[i],NULL,reader,(void *)(uintptr_t)i)) return 1;
    for (unsigned n=1;n<=4096;n++) {
        int core=sched_getcpu();
        if (core>=0 && core<32) atomic_fetch_or(&cpu_mask[2],1u<<core);
        body(hot,n); body(cold,n^0x55aa); hv_jit_finish2(&jit,hot,2,cold,2);
        atomic_store_explicit(&request,n,memory_order_release);
        while (atomic_load_explicit(&ack[0],memory_order_acquire)!=n || atomic_load_explicit(&ack[1],memory_order_acquire)!=n) __asm__ volatile("yield");
    }
    atomic_store_explicit(&request,65536,memory_order_release);
    for (unsigned i=0;i<2;i++) pthread_join(threads[i],NULL);
    checks++;
    printf("JIT_CACHE_CPU_MASK writer=0x%x reader0=0x%x reader1=0x%x\n",atomic_load(&cpu_mask[2]),atomic_load(&cpu_mask[0]),atomic_load(&cpu_mask[1]));
    const unsigned iterations=20000;
    for (unsigned round=0;round<5;round++) {
        uint64_t elapsed[2]={0};
        for (unsigned step=0;step<2;step++) {
            unsigned variant=(round+step)&1; uint64_t start=ns();
            for (unsigned i=0;i<iterations;i++) {
                if (variant) hv_jit_finish2(&jit,hot,16,cold,16);
                else baseline(hot,16,cold,16);
            }
            elapsed[variant]=ns()-start;
        }
        printf("JIT_CACHE_BENCH round=%u iterations=%u baseline_ns_each=%llu candidate_ns_each=%llu\n",round,iterations,
            (unsigned long long)(elapsed[0]/iterations),(unsigned long long)(elapsed[1]/iterations));
    }
    hv_jit_finish(&jit,NULL,0); hv_jit_finish2(&jit,NULL,0,NULL,0);
    hv_jit_region_close(&jit);
    printf("JIT_CACHE_TEST checks=%u failures=%u sequential_exec=11264 smp_rounds=4096 readers=2 no_vm=1\n",checks,atomic_load(&failures));
    return atomic_load(&failures)!=0;
}
