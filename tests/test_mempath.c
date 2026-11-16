/* SPDX-License-Identifier: GPL-2.0-or-later
 * 2026-11-16: translated-access regression for metadata load scheduling.
 * Check actual guest effects/faults, including warm last-byte rechecks.
 */
#include "dispatch.h"
#include "mmu.h"
#include "sysreg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE 0x80000000ull
#define DATA (CODE + 16384)
#define PT 0x90000000ull
static HvMachine m;
static HvCpu cpu;
static uint8_t *ram;
static uint64_t *pt;
static int checks, failures;

static void check(const char *what, uint64_t got, uint64_t want) {
    checks++;
    if (got != want) {
        printf("FAIL %s got=%llx want=%llx\n", what,
               (unsigned long long)got, (unsigned long long)want);
        failures++;
    }
}

static void prepare(unsigned size, int store, int indexed, unsigned ap, int el, int pan) {
    hv_flush_blocks(&m);
    memset(ram, 0x5a, 65536);
    memset(pt, 0, 65536);
    pt[0] = PT + 16384 + 3; pt[2048] = PT + 32768 + 3;
    pt[4096 + 64] = PT + 49152 + 3;
    pt[6144] = CODE | 3 | 3u << 8 | 1u << 10 | (uint64_t)(el ? 0 : 3) << 6;
    pt[6145] = DATA | 3 | 3u << 8 | 1u << 10 | (uint64_t)ap << 6;
    uint32_t insn = size << 30 | (indexed ? 0x38216802u : 0x39000002u) | (!store ? 1u << 22 : 0);
    memcpy(ram, &insn, 4); memcpy(ram + 4, &insn, 4);
    hv_cpu_reset(&cpu, &m);
    cpu.el = el; cpu.spsel = el != 0; cpu.pc = CODE;
    cpu.mair_el1 = 0xff;
    cpu.tcr_el1 = 16 | 2u << 14 | 1u << 23 | 5ull << 32;
    cpu.ttbr0_el1 = PT; cpu.sctlr_el1 |= 1 | 1u << 2 | 1u << 12;
    if (pan) cpu.pstate_extra |= HV_PSTATE_PAN;
    cpu.x[2] = 0x1122334455667788ull;
    cpu.nzcv = 0xa0000000;
    m.stop_pc = CODE + 8; m.stop_pc2 = el ? 0x200 : 0x400;
}

static void access_at(uint64_t va, int indexed) {
    cpu.x[0] = indexed ? DATA : va;
    cpu.x[1] = indexed ? va - DATA : 0;
    check("run halts at access end or exception vector", hv_run(&cpu), HV_EXIT_HALT);
    check("NZCV preserved", cpu.nzcv, 0xa0000000);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (hv_machine_init_cpus(&m, 4u << 20, 1)) return 2;
    ram = aligned_alloc(16384, 65536); pt = aligned_alloc(16384, 65536);
    if (!ram || !pt) return 2;
    hv_machine_add_ram(&m, CODE, 65536, ram, 0);
    hv_machine_add_ram(&m, PT, 65536, pt, 0);
    for (int indexed = 0; indexed < 2; indexed++) for (int store = 0; store < 2; store++)
    for (unsigned size = 0; size < 4; size++) {
        unsigned bytes = 1u << size;
        uint64_t mask = bytes == 8 ? ~0ull : (1ull << (bytes * 8)) - 1;
        prepare(size, store, indexed, 1, 0, 0);
        access_at(DATA + 16384 - bytes, indexed);
        check("last fitting bytes: no fault", cpu.pc, CODE + 8);
        uint64_t v = 0; memcpy(&v, ram + 32768 - bytes, bytes);
        check("last fitting bytes: exact result", store ? v : cpu.x[2],
              (store ? 0x1122334455667788ull : 0x5a5a5a5a5a5a5a5aull) & mask);
        if (bytes > 1) {
            prepare(size, store, indexed, 1, 0, 0);
            access_at(DATA + 16384 - 1, indexed);
            check("crossing unmapped page faults", cpu.pc, 0x400);
            check("crossing page FSC", cpu.esr_el1 & 63, HV_FSC_TRANSLATION(3));
        }
        prepare(size, 1, indexed, 3, 0, 0);
        access_at(DATA + 32, indexed);
        check("read-only store faults", cpu.pc, 0x400);
        check("read-only store FSC", cpu.esr_el1 & 63, HV_FSC_PERMISSION(3));
        check("read-only store leaves data", ram[16384 + 32], 0x5a);
    }
    prepare(3, 0, 0, 1, 1, 1);
    access_at(DATA, 0);
    check("PAN faults", cpu.pc, 0x200);
    check("PAN permission FSC", cpu.esr_el1 & 63, HV_FSC_PERMISSION(3));

    prepare(3, 0, 0, 1, 0, 0);
    access_at(DATA, 0);
    check("initial mapping", cpu.x[2], 0x5a5a5a5a5a5a5a5aull);
    memset(ram + 32768, 0x3c, 16384);
    pt[6145] = (DATA + 16384) | 3 | 3u << 8 | 1u << 10 | 1u << 6;
    hv_sysreg_sys(&cpu, 0xd508871fu, 0); /* TLBI VMALLE1: retire the old addend. */
    cpu.pc = CODE; cpu.halted = HV_HALT_NONE;
    access_at(DATA, 0);
    check("mapping after TLBI uses new addend", cpu.x[2], 0x3c3c3c3c3c3c3c3cull);
    hv_machine_free(&m); free(ram); free(pt);
    printf("MEMPATH_RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
