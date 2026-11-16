#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include "dispatch.h"
#include "platform.h"
#include "cpu_performance.h"
#include "platform_compat.h"

extern const uint32_t lab_arith_begin[], lab_arith_end[];
extern const uint32_t lab_memory_begin[], lab_memory_end[];
extern const uint32_t lab_simd_begin[], lab_simd_end[];
extern const uint32_t lab_timer_begin[], lab_timer_end[];
extern const uint32_t lab_perf_begin[], lab_perf_end[];
static HvMachine machine;
static HvCpu cpu;
static uint8_t *ram;
static int failures, checks;
static _Thread_local unsigned char signal_stack[65536] __attribute__((aligned(16)));

/* Guest code borrows SP. Diagnostics must use an independent signal stack. */
static void signal_hex(const char *key, uint64_t value) {
    char buf[32]; size_t n = 0;
    while (*key && n < sizeof buf - 18) buf[n++] = *key++;
    buf[n++] = '=';
    for (int shift = 60; shift >= 0; shift -= 4) buf[n++] = "0123456789abcdef"[(value >> shift) & 15];
    buf[n++] = '\n';
    (void)write(STDERR_FILENO, buf, n);
}
static void signal_failure(int sig, siginfo_t *info, void *context) {
    ucontext_t *u = context;
    const char title[] = "NATIVE_SIGNAL (host context, not guest PC)\n";
    (void)write(STDERR_FILENO, title, sizeof title - 1);
    signal_hex("signal", (unsigned)sig);
    signal_hex("signal_code", (unsigned)info->si_code);
    signal_hex("address", (uintptr_t)info->si_addr);
    signal_hex("pc", u->uc_mcontext.pc);
    signal_hex("lr", u->uc_mcontext.regs[30]);
    signal_hex("sp", u->uc_mcontext.sp);
    signal_hex("tid", (uint64_t)syscall(SYS_gettid));
    int comm = open("/proc/thread-self/comm", O_RDONLY | O_CLOEXEC);
    if (comm >= 0) {
        char name[64]; ssize_t n = read(comm, name, sizeof name);
        const char label[] = "thread_name=";
        if (n > 0) { (void)write(STDERR_FILENO, label, sizeof label-1); (void)write(STDERR_FILENO, name, (size_t)n); }
        close(comm);
    }
    for (unsigned i = 0; i < 9; ++i) {
        char name[] = "x0"; name[1] = (char)('0' + i);
        signal_hex(name, u->uc_mcontext.regs[i]);
    }
    int self_mem = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    if (self_mem >= 0) {
        /* Read only instruction bytes around PC, never guest data/stack contents.
         * pread fails safely if the generated code has already been unmapped. */
        uintptr_t start = (u->uc_mcontext.pc & ~(uintptr_t)3);
        if (start >= 64) start -= 64;
        signal_hex("code_start", start);
        for (unsigned i = 0; i < 48; ++i) {
            uint32_t word;
            if (pread(self_mem, &word, sizeof word, (off_t)(start+i*4)) != sizeof word) break;
            signal_hex("code_word", word);
        }
        uintptr_t fp = u->uc_mcontext.regs[29], sp = u->uc_mcontext.sp;
        for (unsigned depth = 0; depth < 24; ++depth) {
            uint64_t pair[2];
            if ((fp & 15) || fp < sp || fp - sp > 16 * 1024 * 1024 ||
                pread(self_mem, pair, sizeof pair, (off_t)fp) != sizeof pair) break;
            signal_hex("return_pc", pair[1]);
            if (pair[0] <= fp) break;
            fp = pair[0];
        }
        close(self_mem);
    }
    /* No allocator, stdio or symbol resolver in a signal handler. Record executable
       mappings so a host-side symbolizer can resolve the fault after the worker exits. */
    int maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (maps >= 0) {
        const char begin[] = "NATIVE_EXEC_MAPS_BEGIN\n", end[] = "NATIVE_EXEC_MAPS_END\n";
        (void)write(STDERR_FILENO, begin, sizeof begin - 1);
        char bytes[4096], line[1024]; size_t used = 0, total = 0; ssize_t n;
        while (total < 16 * 1024 * 1024 && (n = read(maps, bytes, sizeof bytes)) > 0) {
            total += (size_t)n;
            for (ssize_t i = 0; i < n; ++i) {
                if (used < sizeof line) line[used++] = bytes[i];
                if (bytes[i] == '\n') {
                    size_t p = 0; while (p < used && line[p] != ' ') ++p;
                    if (p + 3 < used && line[p + 3] == 'x') (void)write(STDERR_FILENO, line, used);
                    used = 0;
                }
            }
        }
        close(maps); (void)write(STDERR_FILENO, end, sizeof end - 1);
    }
    if (hv_self) {
        signal_hex("guest_pc", hv_self->pc);
        signal_hex("guest_miss", hv_self->miss_site);
        signal_hex("guest_elr", hv_self->elr_el1);
        signal_hex("guest_esr", hv_self->esr_el1);
        uintptr_t base = (uintptr_t)hv_self->m->jit.base;
        if (u->uc_mcontext.pc >= base && u->uc_mcontext.pc - base < hv_self->m->jit.capacity * 4) {
            signal_hex("jit_offset", u->uc_mcontext.pc - base);
            signal_hex("jit_word", *(const uint32_t *)(uintptr_t)u->uc_mcontext.pc);
        }
    }
    _exit(128 + sig);
}
int lab_thread_diagnostics(void) {
    stack_t stack = {.ss_sp = signal_stack, .ss_size = sizeof signal_stack};
    return sigaltstack(&stack, NULL);
}
static void install_diagnostics(void) {
    if (lab_thread_diagnostics()) { perror("sigaltstack"); exit(1); }
    struct sigaction action = {.sa_sigaction = signal_failure, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&action.sa_mask);
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGALRM};
    for (unsigned i = 0; i < sizeof signals / sizeof signals[0]; i++) sigaction(signals[i], &action, NULL);
}

int lab_aes_selftest(void);

static void check(const char *name, int ok) {
    checks++;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += !ok;
}

static int guest(const uint32_t *begin, const uint32_t *end) {
    hv_flush_blocks(&machine);
    size_t bytes = (const uint8_t *)end - (const uint8_t *)begin + 4;
    memcpy(ram, begin, bytes);
    hv_cpu_reset(&cpu, &machine);
    cpu.cpacr_el1 = 3u << 20;
    cpu.pc = 0x80000000ull;
    cpu.sp = 0x8001f000ull;
    machine.stop_pc = cpu.pc + bytes - 4;
    int result = hv_run(&cpu);
    if (result != HV_EXIT_HALT)
        printf("guest exit=%d pc=%" PRIx64 " esr=%" PRIx64 " far=%" PRIx64 "\n", result, cpu.pc, cpu.esr_el1, cpu.far_el1);
    return result == HV_EXIT_HALT;
}

static int cpu_performance_test(void) {
    cpu_set_t original, fast;
    if (sched_getaffinity(0,sizeof original,&original)) { perror("getaffinity self"); return 1; }
    int eligible = lab_cpu_fast_mask(&fast);
    if (eligible < 2) { fprintf(stderr,"CPU_PERF_UNAVAILABLE eligible=%d errno=%d\n",eligible,errno); return 2; }
    if (hv_machine_init_cpus(&machine,8u << 20,1)) return 1;
    ram = aligned_alloc(16384,128u << 10);
    if (!ram) { hv_machine_free(&machine); return 1; }
    memset(ram,0,128u << 10);
    hv_machine_add_ram(&machine,0x80000000ull,128u << 10,ram,0);
    /* Authored load/increment/store loop: 2,097,151 iterations. */
    int failed = 0;
    for (unsigned run=0; run<4; run++) {
        int high = run==1 || run==2;
        if (sched_setaffinity(0,sizeof original,high ? &fast : &original)) { failed=1; break; }
        memset(ram+65536,0,8);
        uint64_t start=hv_host_ticks();
        int ok=guest(lab_perf_begin,lab_perf_end);
        uint64_t elapsed=hv_host_ticks()-start;
        uint64_t value; memcpy(&value,ram+65536,8);
        printf("CPU_PERF run=%u cluster=%s eligible=%d core=%d elapsed_us=%" PRIu64 " value=%" PRIu64 " exact=%d\n",run,high ? "high" : "default",eligible,sched_getcpu(),elapsed/24,value,ok && value==2097151);
        if (!ok || value!=2097151) { failed=1; break; }
    }
    if (sched_setaffinity(0,sizeof original,&original)) failed=1;
    free(ram); ram=NULL; hv_machine_free(&machine);
    return failed;
}

static int selftest(void) {
    check("portable AES ECB/CBC known-answer vectors", lab_aes_selftest() == 0);
    HvJitRegion jit;
    if (hv_jit_region_open(&jit, 16384)) { perror("JIT mapping"); return 1; }
    const uint32_t body[] = {0xd2800540, 0xd65f03c0};
    uint32_t *entry = hv_jit_reserve(&jit, 2);
    hv_jit_write(&jit, entry, body, sizeof body);
    hv_jit_finish(&jit, entry, 2);
    check("dual-mapped JIT returns 42", ((long (*)(void))entry)() == 42);
    uint32_t changed = 0xd2800c60; /* mov x0, #99 */
    hv_jit_write(&jit, entry, &changed, 4);
    hv_jit_finish_branch(&jit, entry);
    check("rewritten JIT returns 99", ((long (*)(void))entry)() == 99);
    hv_jit_region_close(&jit);
    if (hv_machine_init(&machine, 8u << 20)) { perror("machine init"); return 1; }
    ram = aligned_alloc(16384, 128u << 10);
    if (!ram) { perror("guest memory"); hv_machine_free(&machine); return 1; }
    memset(ram, 0, 128u << 10);
    hv_machine_add_ram(&machine, 0x80000000ull, 128u << 10, ram, 0);
    uintptr_t before, after;
    __asm__ volatile("mov %0, x18" : "=r"(before));
    int ran = guest(lab_arith_begin, lab_arith_end);
    __asm__ volatile("mov %0, x18" : "=r"(after));
    check("integer arithmetic and conditional branches", ran && cpu.x[0] == 42 && cpu.x[2] == 55);
    check("guest scratch and platform registers", ran && cpu.x[15] == 24 && cpu.x[18] == 77 && cpu.x[28] == 80);
    check("Android host x18 preserved", before == after);
    ran = guest(lab_memory_begin, lab_memory_end);
    check("guest memory load/store", ran && cpu.x[2] == 1234);
    check("guest exclusive load/store", ran && cpu.x[4] == 0 && cpu.x[5] == 1235);
    ran = guest(lab_simd_begin, lab_simd_end);
    check("floating point", ran && (uint64_t)cpu.v[2] == 0x400c000000000000ull);
    __uint128_t vector = ((__uint128_t)0x0000000e0000000eull << 64) | 0x0000000e0000000eull;
    check("SIMD registers", ran && cpu.v[4] == vector);
    ran = guest(lab_timer_begin, lab_timer_end);
    check("portable 24 MHz guest counter", ran && cpu.x[0] == 24000000 && cpu.x[2] >= cpu.x[1]);
    uint64_t ticks = hv_host_ticks();
    usleep(20000);
    uint64_t elapsed = hv_host_ticks() - ticks;
    check("host clock time domain", elapsed >= 240000 && elapsed < 24000000);
    hv_machine_free(&machine);
    free(ram);
    printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}

int lab_boot_firmware(const char *rom, unsigned ram_mb, unsigned seconds, int probe_dfu);
int lab_disk_init(const char *dir);
int lab_disk_test(const char *dir);
int lab_network_probe(void);
int lab_smp_test(void);
int lab_smp4_test(void);
int lab_restore(const char *rom, const char *ipsw, const char *dir, unsigned ram_mb, unsigned seconds);
int lab_boot_disk(const char *rom, const char *dir, unsigned ram_mb, unsigned seconds);
int lab_boot_reims(const char *rom, const char *dir, unsigned ram_mb, unsigned seconds, const char *library, const char *loader, unsigned ncpus);

/* Compare persistent CPU-cache reservation with the same JIT size and allocator.
 * Does not open firmware/disks or run the guest. */
static int cache_memory(unsigned capacity) {
    if (hv_machine_init_cpus(&machine, 256u << 20, capacity)) return 1;
    unsigned allocated = 0;
    for (unsigned i=0; i<HV_MAX_CPUS; i++) allocated += machine.percpu[i].ijc != NULL;
    FILE *f=fopen("/proc/self/smaps_rollup", "r");
    if (f) {
        char line[256];
        while (fgets(line,sizeof line,f))
            if (!strncmp(line,"Pss:",4) || !strncmp(line,"SwapPss:",8)) printf("CACHE_MEMORY %s",line);
        fclose(f);
    }
    printf("CACHE_RESERVATION requested=%u allocated=%u\n",capacity,allocated);
    hv_machine_free(&machine);
    return allocated != capacity;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    install_diagnostics();
    if (argc == 2 && !strcmp(argv[1], "diagnostic-signal")) { raise(SIGSEGV); return 2; }
    alarm(45);
    struct utsname u;
    uname(&u);
    struct sysinfo info;
    sysinfo(&info);
    printf("Henge Android research / upstream e98920c\n");
    printf("HOST arch=%s kernel=%s page_bytes=%ld ram_bytes=%" PRIu64 "\n", u.machine, u.release,
           sysconf(_SC_PAGESIZE), (uint64_t)info.totalram * info.mem_unit);
    printf("HOST hwcap=0x%lx hwcap2=0x%lx\n", getauxval(AT_HWCAP), getauxval(AT_HWCAP2));
    if (argc == 2 && !strcmp(argv[1], "platform-probe")) return lab_platform_probe();
    if (argc > 1 && (!strcmp(argv[1],"boot-reims") || !strcmp(argv[1],"boot-disk") || !strcmp(argv[1],"restore")))
        if (lab_platform_probe()) return 77;
    if (argc == 1 || !strcmp(argv[1], "selftest")) return selftest();
    if (!strcmp(argv[1], "probe")) return 0;
    if (!strcmp(argv[1], "cpu-perf-test")) return cpu_performance_test();
    if (argc==3 && !strcmp(argv[1], "cache-memory")) return cache_memory((unsigned)atoi(argv[2]));
    if (!strcmp(argv[1], "smp-test")) return lab_smp_test();
    if (!strcmp(argv[1], "smp4-test")) return lab_smp4_test();
    if (!strcmp(argv[1], "net-probe")) return lab_network_probe();
    if (argc == 3 && !strcmp(argv[1], "disk-init")) return lab_disk_init(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "disk-test")) return lab_disk_test(argv[2]);
    if ((argc == 8 || argc == 9) && !strcmp(argv[1], "boot-reims")) {
        unsigned seconds = (unsigned)atoi(argv[5]);
        if (argc == 9 && (strlen(argv[8]) != 1 || argv[8][0] < '1' || argv[8][0] > '4')) return 2;
        unsigned ncpus = argc == 9 ? (unsigned)(argv[8][0] - '0') : 1;
        if ((seconds == 0 && strcmp(argv[5], "0")) || (seconds != 0 && (seconds < 10 || seconds > 7200))) return 2;
        alarm(seconds ? seconds + 30 : 0);
        return lab_boot_reims(argv[2], argv[3], (unsigned)atoi(argv[4]), seconds, argv[6], argv[7], ncpus);
    }
    if (argc >= 4 && !strcmp(argv[1], "boot-disk")) {
        unsigned seconds = argc > 5 ? (unsigned)atoi(argv[5]) : 120;
        if (seconds < 10 || seconds > 7200) return 2;
        alarm(seconds + 30);
        return lab_boot_disk(argv[2], argv[3], argc > 4 ? (unsigned)atoi(argv[4]) : 2048, seconds);
    }
    if (argc >= 5 && !strcmp(argv[1], "restore")) {
        unsigned seconds = argc > 6 ? (unsigned)atoi(argv[6]) : 180;
        if (seconds && (seconds < 10 || seconds > 7200)) return 2;
        alarm(seconds ? seconds + 30 : 0);
        return lab_restore(argv[2], argv[3], argv[4], argc > 5 ? (unsigned)atoi(argv[5]) : 2048, seconds);
    }
    if (argc >= 3 && (!strcmp(argv[1], "rom") || !strcmp(argv[1], "dfu")))
        return lab_boot_firmware(argv[2], argc > 3 ? (unsigned)atoi(argv[3]) : 1024,
                                 argc > 4 ? (unsigned)atoi(argv[4]) : 15, !strcmp(argv[1], "dfu"));
    fprintf(stderr, "usage: %s [probe|selftest|net-probe|rom|dfu FILE [RAM_MB] [SECONDS]|disk-init DIR|disk-test DIR|restore ROM IPSW DIR [RAM_MB] [SECONDS]|boot-disk ROM DIR [RAM_MB] [SECONDS]]\n", argv[0]);
    return 2;
}
