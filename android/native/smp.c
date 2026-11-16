/* Authored two-CPU PSCI/release-acquire fixture. No firmware or disk is opened. */
#include "dispatch.h"
#include "platform.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern const uint32_t lab_smp_primary[], lab_smp_primary_end[];
extern const uint32_t lab_smp_secondary[], lab_smp_secondary_end[];
extern const uint32_t lab_smp4_primary[], lab_smp4_primary_end[];
extern const uint32_t lab_smp4_secondary[], lab_smp4_secondary_end[];
extern int lab_thread_diagnostics(void);
static HvMachine sm;
static HvCpu sc[4];
static atomic_int done;
static int thread_platform_ok[4];

static void *run_secondary(void *arg) {
    HvCpu *cpu = arg ? arg : &sc[1];
    if (lab_thread_diagnostics()) { hv_machine_halt(&sm, HV_HALT_HOST); return (void *)-1; }
    uintptr_t before, after;
    __asm__ volatile("mov %0, x18" : "=r"(before));
    int result = hv_run(cpu);
    __asm__ volatile("mov %0, x18" : "=r"(after));
    thread_platform_ok[cpu->index] = before == after;
    return (void *)(intptr_t)result;
}
static void *watchdog(void *unused) {
    for (unsigned i = 0; i < 5000 && !atomic_load(&done); ++i) {
        usleep(1000);
        hv_machine_kick_all(&sm);
    }
    if (!atomic_load(&done)) hv_machine_halt(&sm, HV_HALT_HOST);
    return NULL;
}
int lab_smp_test(void) {
    uint8_t *memory = aligned_alloc(16384, 65536);
    if (!memory || hv_machine_init_cpus(&sm, 4 << 20, 2)) { free(memory); return 1; }
    memset(memory, 0, 65536);
    memcpy(memory, lab_smp_primary, (const uint8_t *)lab_smp_primary_end - (const uint8_t *)lab_smp_primary);
    memcpy(memory + 0x4000, lab_smp_secondary, (const uint8_t *)lab_smp_secondary_end - (const uint8_t *)lab_smp_secondary);
    hv_machine_add_ram(&sm, 0x80000000, 65536, memory, 0);
    for (unsigned i = 0; i < 2; ++i) {
        hv_cpu_reset_index(&sc[i], &sm, i);
        sc[i].mpidr = 0x80000000u | i;
        sc[i].pc = 0x80000000;
        sc[i].off = i != 0;
    }
    pthread_t secondary, timer;
    if (pthread_create(&secondary, NULL, run_secondary, NULL)) return 1;
    if (pthread_create(&timer, NULL, watchdog, NULL)) {
        hv_machine_halt(&sm, HV_HALT_HOST); pthread_join(secondary, NULL); return 1;
    }
    uintptr_t before, after;
    __asm__ volatile("mov %0, x18" : "=r"(before));
    int primary_result = hv_run(&sc[0]);
    __asm__ volatile("mov %0, x18" : "=r"(after));
    thread_platform_ok[0] = before == after;
    hv_machine_halt(&sm, HV_HALT_HOST);
    atomic_store(&done, 1);
    void *secondary_result;
    pthread_join(secondary, &secondary_result);
    pthread_join(timer, NULL);
    const uint64_t *data = (const uint64_t *)(memory + 0x8000);
    int failed = primary_result != HV_EXIT_HALT || (intptr_t)secondary_result != HV_EXIT_HALT ||
        data[0] != 0 || data[1] != 0x42 || data[2] != 1 ||
        sc[0].halted != HV_HALT_SHUTDOWN || sc[1].halted != HV_HALT_SHUTDOWN ||
        !thread_platform_ok[0] || !thread_platform_ok[1];
    printf("SMP_RESULT failures=%d cpu_on=%lld context=%llu release=%llu x18=%d,%d primary_exit=%d secondary_exit=%ld shutdown=%d,%d\n",
        failed, (long long)data[0], (unsigned long long)data[1], (unsigned long long)data[2],
        thread_platform_ok[0], thread_platform_ok[1], primary_result, (long)(intptr_t)secondary_result,
        sc[0].halted, sc[1].halted);
    hv_machine_free(&sm); free(memory);
    return failed;
}

int lab_smp4_test(void) {
    uint8_t *memory=aligned_alloc(16384,65536);
    if (!memory || hv_machine_init_cpus(&sm,4u<<20,4)) { free(memory); return 1; }
    memset(memory,0,65536); atomic_store(&done,0);
    memcpy(memory,lab_smp4_primary,(const uint8_t *)lab_smp4_primary_end-(const uint8_t *)lab_smp4_primary);
    memcpy(memory+0x4000,lab_smp4_secondary,(const uint8_t *)lab_smp4_secondary_end-(const uint8_t *)lab_smp4_secondary);
    hv_machine_add_ram(&sm,0x80000000,65536,memory,0);
    for (unsigned i=0;i<4;i++) {
        hv_cpu_reset_index(&sc[i],&sm,i); sc[i].mpidr=0x80000000u|i;
        sc[i].pc=0x80000000; sc[i].off=i!=0;
    }
    pthread_t secondary[3],timer;
    unsigned created=0; int failed=0;
    for (unsigned i=1;i<4;i++) {
        if (pthread_create(&secondary[created],NULL,run_secondary,&sc[i])) { failed=1; break; }
        created++;
    }
    int timer_created=!failed && !pthread_create(&timer,NULL,watchdog,NULL);
    if (!timer_created) failed=1;
    int primary=failed ? -1 : hv_run(&sc[0]);
    hv_machine_halt(&sm,HV_HALT_HOST); atomic_store(&done,1);
    for (unsigned i=0;i<created;i++) {
        void *result; pthread_join(secondary[i],&result);
        failed|=(intptr_t)result!=HV_EXIT_HALT || !thread_platform_ok[i+1];
    }
    if (timer_created) pthread_join(timer,NULL);
    const uint64_t *data=(const uint64_t *)(memory+0x8000);
    failed|=primary!=HV_EXIT_HALT || data[0]!=1;
    for (unsigned i=1;i<4;i++) failed|=data[i]!=0 || data[8+i]!=i || data[16+i]!=i+64;
    for (unsigned i=0;i<4;i++) failed|=sc[i].halted!=HV_HALT_SHUTDOWN;
    printf("SMP4_RESULT failures=%d contexts=%llu,%llu,%llu completed=%llu,%llu,%llu shutdown=%d,%d,%d,%d\n",
        failed,(unsigned long long)data[9],(unsigned long long)data[10],(unsigned long long)data[11],
        (unsigned long long)data[17],(unsigned long long)data[18],(unsigned long long)data[19],
        sc[0].halted,sc[1].halted,sc[2].halted,sc[3].halted);
    hv_machine_free(&sm);free(memory);return failed;
}
