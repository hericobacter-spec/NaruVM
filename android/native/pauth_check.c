/* Focused execution of the upstream tests/jit_programs.S PAC fixture.
   This tests the engine's unsigned-pointer contract, not native signing. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dispatch.h"

extern const uint8_t lab_pac_begin[], lab_pac_end[];
static HvMachine m;
static HvCpu cpu;

int main(void) {
    alarm(30);
    if (hv_machine_init(&m, 8u << 20)) return 1;
    uint8_t *ram = aligned_alloc(16384, 128u << 10);
    if (!ram) return 1;
    memset(ram, 0, 128u << 10);
    size_t bytes = lab_pac_end - lab_pac_begin;
    if (bytes > 16384) return 1;
    memcpy(ram, lab_pac_begin, bytes);
    hv_machine_add_ram(&m, 0x80000000ull, 128u << 10, ram, 0);
    m.stop_pc = 0x80000000ull + bytes;
    unsigned failed = 0;
    for (unsigned seed = 0; seed < 2; seed++) {
        uint64_t want[31];
        hv_cpu_reset(&cpu, &m);
        cpu.pc = 0x80000000ull; cpu.sp = 0x8001f000ull;
        for (unsigned r = 0; r < 31; r++)
            want[r] = cpu.x[r] = ((r + seed) & 1 ? 0xfffffe0007123450ull : 0x100004000ull) + r * 8;
        want[4] = 0; want[9] = want[10] = want[30];
        int result = hv_run(&cpu);
        failed += result != HV_EXIT_HALT;
        for (unsigned r = 0; r < 31; r++) if (cpu.x[r] != want[r]) {
            fprintf(stderr, "PAC_MISMATCH seed=%u x%u expected=%" PRIx64 " actual=%" PRIx64 "\n",
                    seed, r, want[r], cpu.x[r]);
            failed++;
        }
    }
    printf("PAUTH_RESULT seeds=2 registers_per_seed=31 failures=%u\n", failed);
    hv_machine_free(&m); free(ram);
    return failed ? 1 : 0;
}
