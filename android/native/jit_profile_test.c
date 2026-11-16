#include "jit_profile.h"
#include <assert.h>
static HvMachine machine;
static int run(const char *request, const char *expected);
#ifdef HENGE_JIT_PROFILE_INTEGRATION
#include "dispatch.h"
static HvCpu cpu;
static void integration(void) {
    memset(&machine, 0, sizeof machine);
    assert(setenv("HV_JIT_SPANS", "1", 1) == 0);
    assert(hv_machine_init(&machine, 4u << 20) == 0);
    uint32_t *code = aligned_alloc(16384, 16384);
    assert(code); memset(code, 0, 16384);
    code[0]=0xd2800060; code[1]=0xf1000400; code[2]=0x54ffffe1; code[3]=0xd4400000;
    hv_machine_add_ram(&machine, 0x80000000, 16384, code, 1);
    hv_cpu_reset(&cpu, &machine); cpu.pc=0x80000000;
    machine.stop_pc=0x8000000c;
    assert(hv_run(&cpu) == HV_EXIT_HALT && cpu.x[0] == 0);
    assert(machine.nblocks >= 2);
    for (int i=0; i<machine.nblocks; ++i) assert(machine.jit_spans[i].hot_words > 0);
    char request[256];
    snprintf(request, sizeof request, "HENGE_JIT_PROFILE_V1 123 %u 99 1\n%" PRIxPTR " 1\n",
        machine.jit_gen, (uintptr_t)machine.blocks[0].code);
    assert(run(request, "block ") == 0);
    hv_machine_free(&machine); free(code);
    puts("JIT_PROFILE_INTEGRATION failures=0 (real emit, temporary block publication, exact lookup)");
}
#endif

static HvBlock blocks[2];
static HvJitSpan spans[2];

static int run(const char *request, const char *expected) {
    FILE *in = tmpfile(), *out = tmpfile();
    assert(in && out);
    fputs(request, in); rewind(in);
    int result = lab_jit_profile_write(&machine, in, out, 123);
    rewind(out);
    char text[4096] = {0};
    assert(fread(text, 1, sizeof text - 1, out) < sizeof text - 1);
    assert(strstr(text, expected));
    fclose(in); fclose(out);
    return result;
}

int main(void) {
    machine.jit.base = (uint32_t *)(uintptr_t)0x1000;
    machine.jit.used = 0x100 / 4;
    machine.jit_gen = 7;
    machine.nblocks = 2; machine.blocks = blocks; machine.jit_spans = spans;
    /* Deliberately unsorted, as two vCPU chunks interleave translations. */
    blocks[1] = (HvBlock){.jit_gen=7, .va=0x8000, .code=(uint32_t *)(uintptr_t)0x1020, .ninsns=4};
    blocks[0] = (HvBlock){.jit_gen=7, .va=0x9000, .code=(uint32_t *)(uintptr_t)0x1080, .flags=HV_BLOCK_EL1, .ninsns=8};
    spans[1] = (HvJitSpan){.hot_words=4};
    spans[0] = (HvJitSpan){.hot_words=8, .cold=(uint32_t *)(uintptr_t)0x10c0, .cold_words=8};
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 3\n1010 1\n1024 2\n1088 3\n", "block 0x1088 0x9000 1 8 2 hot 3") == 0);
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 1\n10c8 2\n", "block 0x10c8 0x9000 1 8 2 cold 2") == 0);
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 1\n1060 1\n", "outside 0x1060 1") == 0);
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 2\n1100 1\nfff 2\n", "outside 0x1100 1") == 0);
    assert(run("HENGE_JIT_PROFILE_V1 123 6 99 0\n", "stale_generation") == -1);
    assert(run("HENGE_JIT_PROFILE_V1 124 7 99 0\n", "stale_generation") == -1);
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 4097\n", "invalid_request") == -1);
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 1\n", "truncated_request") == -1);
    blocks[1].jit_gen=6;
    assert(run("HENGE_JIT_PROFILE_V1 123 7 99 1\n1080 1\n", "block_generation") == -1);
    puts("JIT_PROFILE_TEST failures=0 (bounds, lookup, PID, generation, malformed input)");
#ifdef HENGE_JIT_PROFILE_INTEGRATION
    integration();
#endif
}
