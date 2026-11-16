#include "dispatch.h"
#include "cpu_performance.h"
#include "platform.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void lab_cpu_memory_begin(uint64_t *, uint64_t), lab_cpu_memory_end(void);
extern void lab_cpu_alu_begin(uint64_t *, uint64_t), lab_cpu_alu_end(void);
extern void lab_cpu_chase_begin(uint64_t *, uint64_t), lab_cpu_chase_end(void);
static HvMachine machine;
static HvCpu cpu;
static uint8_t *ram;
static int mmu;

static void seed(unsigned workload, void *data) {
    memset(data,0,65536);
    if (workload==2) for (unsigned i=0;i<8192;i++)
        ((uint64_t *)data)[i]=((i*5+1)&8191)*8;
}

static uint64_t translated(unsigned workload, size_t code_bytes, uint64_t iterations, uint64_t *value) {
    hv_cpu_reset(&cpu, &machine);
    cpu.el=0; cpu.spsel=0; cpu.pc=0x80000000;
    cpu.x[0]=0x80010000; cpu.x[1]=iterations;
    if (mmu) {
        cpu.mair_el1=0xff;
        cpu.tcr_el1=16 | 2u<<14 | 1u<<23 | 5ull<<32;
        cpu.ttbr0_el1=0x90000000;
        cpu.sctlr_el1|=1 | 1u<<2 | 1u<<12;
    }
    machine.stop_pc=cpu.pc+code_bytes;
    seed(workload,ram+65536);
    uint64_t start=hv_host_ticks();
    int result=hv_run(&cpu);
    uint64_t elapsed=hv_host_ticks()-start;
    if (result != HV_EXIT_HALT) { fprintf(stderr,"BENCH_BAD_EXIT %d\n",result); exit(1); }
    memcpy(value,ram+65536,8);
    return elapsed;
}

int main(int argc, char **argv) {
    if (argc!=3 || (strcmp(argv[1],"--superblocks=0") && strcmp(argv[1],"--superblocks=1")) ||
        (strcmp(argv[2],"--mmu=off") && strcmp(argv[2],"--mmu=on"))) return 2;
    mmu=!strcmp(argv[2],"--mmu=on");
    setvbuf(stdout,NULL,_IONBF,0);
    setenv("HV_SUPERBLOCKS",argv[1]+14,1);
    if (lab_cpu_performance(0)<2 || hv_machine_init(&machine,8u<<20)) return 1;
    ram=aligned_alloc(16384,128u<<10);
    if (!ram) return 1;
    memset(ram,0,128u<<10);
    hv_machine_add_ram(&machine,0x80000000,128u<<10,ram,0);
    uint64_t *pt=aligned_alloc(16384,65536), *native_data=aligned_alloc(16384,65536);
    if (!pt || !native_data) return 1;
    memset(pt,0,65536);
    pt[0]=0x90004003; pt[2048]=0x90008003; pt[4096+64]=0x9000c003;
    for (unsigned i=0;i<8;i++) pt[6144+i]=(0x80000000ull+(uint64_t)i*16384)|3|3u<<8|1u<<10|(i==0?3u:1u)<<6;
    hv_machine_add_ram(&machine,0x90000000,65536,pt,0);
    const struct { const char *name; void (*begin)(uint64_t *,uint64_t); void (*end)(void); } cases[]={
        {"memory",lab_cpu_memory_begin,lab_cpu_memory_end},
        {"alu",lab_cpu_alu_begin,lab_cpu_alu_end},
        {"pointer_chase",lab_cpu_chase_begin,lab_cpu_chase_end}};
    int failures=0;
    for (unsigned test=0;test<3;test++) {
        hv_flush_blocks(&machine);
        size_t bytes=(uintptr_t)cases[test].end-(uintptr_t)cases[test].begin;
        if (!bytes || bytes>16384) return 1;
        memcpy(ram,(const void *)cases[test].begin,bytes);
        uint64_t warm;
        translated(test,bytes,1000,&warm);
        seed(test,native_data);
        cases[test].begin(native_data,2097151);
        translated(test,bytes,2097151,&warm);
        for (unsigned round=0;round<3;round++) {
            uint64_t native_value=0,jit_value=0,tn=0,tj=0;
            /* Alternate order; native and translated loops use the same
             * authored instructions and iteration count on this tablet. */
            for (unsigned order=0;order<2;order++) {
                if ((round+order)%2==0) {
                    seed(test,native_data);
                    uint64_t start=hv_host_ticks();
                    cases[test].begin(native_data,2097151);
                    tn=hv_host_ticks()-start;
                    native_value=native_data[0];
                } else tj=translated(test,bytes,2097151,&jit_value);
            }
            int exact=native_value==jit_value && (test!=0 || jit_value==2097151);
            printf("NATIVE_JIT_BENCH workload=%s superblocks=%s mmu=%s round=%u native_us=%" PRIu64
                   " jit_us=%" PRIu64 " exact=%d blocks=%d\n",
                cases[test].name,argv[1]+14,mmu?"on":"off",round,tn/24,tj/24,exact,machine.nblocks);
            failures+=!exact;
        }
    }
    hv_machine_free(&machine);free(ram);free(pt);free(native_data);
    printf("NATIVE_JIT_RESULT failures=%d guest_os=none scope=authored_loops_only\n",failures);
    return failures?1:0;
}
