#include "dispatch.h"
#include "sysreg.h"
#include "platform.h"
#include "cpu_performance.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Authored instructions only; no guest disk, firmware or account access. */
static HvMachine machine;
static HvCpu cpu;
static uint32_t *ram;
static int checks, failures;
#define BASE UINT64_C(0x80000000)
static void check(const char *name, int ok, unsigned variant, unsigned rt) {
    checks++;
    if (!ok) { failures++; fprintf(stderr,"FAIL %s variant=%u rt=%u\n",name,variant,rt); }
}
static void prepare(unsigned el) {
    hv_flush_blocks(&machine);
    hv_cpu_reset(&cpu,&machine);
    cpu.el=el; cpu.spsel=el; cpu.pc=BASE;
    cpu.vbar_el1=BASE+16384; cpu.cntvoff=UINT64_C(0x12345678);
    cpu.sp=UINT64_C(0x76543000); cpu.nzcv=UINT64_C(0xa0000000);
    cpu.fpcr=1u<<22; cpu.fpsr=0x0800001f;
    for (unsigned i=0;i<31;i++) cpu.x[i]=UINT64_C(0x5310000000000000)+i*UINT64_C(0x11111111111);
    for (unsigned i=0;i<32;i++) cpu.v[i]=((__uint128_t)(UINT64_C(0xfedcba9800000000)+i)<<64)|(UINT64_C(0x1234567800000000)+i);
    machine.stop_pc=BASE+4; machine.stop_pc2=BASE+16384+0x400;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    lab_cpu_performance(0);
    if (hv_machine_init(&machine,8u<<20)) return 1;
    ram=aligned_alloc(16384,32768);
    if (!ram) return 1;
    memset(ram,0,32768);
    hv_machine_add_ram(&machine,BASE,32768,(uint8_t *)ram,0);
    const uint32_t mrs[]={0xd53be020,0xd53be0a0,0xd53be040,0xd53be0c0};
    for (unsigned kind=0;kind<4;kind++) for (unsigned variant=0;variant<5;variant++) for (unsigned rt=0;rt<32;rt++) {
        unsigned el=variant==4;
        prepare(el); ram[0]=mrs[kind]|rt;
        cpu.cntkctl_el1=el ? 0 : variant;
        cpu.el0_cnt=(uint32_t)cpu.cntkctl_el1;
        uint64_t saved[31]; __uint128_t vectors[32];
        memcpy(saved,cpu.x,sizeof saved); memcpy(vectors,cpu.v,sizeof vectors);
        uint64_t before=hv_counter(); int result=hv_run(&cpu); uint64_t after=hv_counter();
        int trapped=!el && !(cpu.cntkctl_el1 & (1u<<(kind>=2)));
        check("exit",result==HV_EXIT_HALT,variant,rt);
        check("permission",cpu.pc==(trapped ? BASE+16384+0x400 : BASE+4),variant,rt);
        /* Match the existing helper's undefined-instruction denial. This
           fixture does not claim to fix its architectural ESR encoding. */
        if (trapped) check("same helper denial syndrome",(cpu.esr_el1>>26)==0,variant,rt);
        else if (rt!=31) {
            uint64_t value=cpu.x[rt]+(kind>=2 ? cpu.cntvoff : 0);
            check("same monotonic clock",value>=before && value<=after,variant,rt);
        }
        int registers=1;
        for (unsigned i=0;i<31;i++) if ((trapped || i!=rt) && cpu.x[i]!=saved[i]) registers=0;
        check("all other GPRs",registers,variant,rt);
        check("all SIMD bits",!memcmp(vectors,cpu.v,sizeof vectors),variant,rt);
        check("NZCV",cpu.nzcv==UINT64_C(0xa0000000),variant,rt);
        check("FP control/status",cpu.fpcr==(1u<<22) && cpu.fpsr==0x0800001f,variant,rt);
        if (!trapped) check("guest SP",cpu.sp==UINT64_C(0x76543000),variant,rt);
    }
    for (unsigned kind=0;kind<2;kind++) for (unsigned round=0;round<6;round++) {
        prepare(0); cpu.cntkctl_el1=3; cpu.el0_cnt=3;
        ram[0]=(kind ? mrs[2] : mrs[0])|2; ram[1]=0xf1000421; ram[2]=0x54ffffc1;
        cpu.x[1]=round ? 200000 : 1000; machine.stop_pc=BASE+12;
        uint64_t before=hv_counter(); int result=hv_run(&cpu); uint64_t after=hv_counter();
        uint64_t value=cpu.x[2]+(kind ? cpu.cntvoff : 0);
        int valid=result==HV_EXIT_HALT && !cpu.x[1] && value>=before && value<=after;
        check("loop clock and completion",valid,kind,round);
        if (round) printf("COUNTER_BENCH fast=%d kind=%s round=%u iterations=200000 elapsed_us=%" PRIu64 " exact=%d\n",
            hv_knob[HV_K_PORTABLE_COUNTER],kind?"virtual":"physical",round,(after-before)/24,valid);
    }
    hv_machine_free(&machine); free(ram);
    printf("COUNTER_RESULT checks=%d failures=%d scope=authored_clock_reads_only\n",checks,failures);
    return failures ? 1 : 0;
}
