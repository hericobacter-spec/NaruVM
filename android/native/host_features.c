/* Standalone read-only diagnostic using Linux's HWCAP_CPUID MRS ABI.
   Values are kernel-filtered system-safe IDs, not unrestricted hardware IDs.
   https://docs.kernel.org/arch/arm64/cpu-feature-registers.html */
#include <asm/hwcap.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/auxv.h>

int main(void) {
    unsigned long caps = getauxval(AT_HWCAP);
    if (!(caps & HWCAP_CPUID)) {
        fprintf(stderr, "HWCAP_CPUID unavailable; no ID register read attempted\n");
        return 2;
    }
    uint64_t isar0, isar1, pfr0;
    __asm__ volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r"(isar0));
    __asm__ volatile("mrs %0, ID_AA64ISAR1_EL1" : "=r"(isar1));
    __asm__ volatile("mrs %0, ID_AA64PFR0_EL1" : "=r"(pfr0));
    printf("{\"hwcap\":\"0x%lx\",\"hwcap2\":\"0x%lx\","
           "\"isar0\":\"0x%016" PRIx64 "\",\"isar1\":\"0x%016" PRIx64
           "\",\"pfr0\":\"0x%016" PRIx64 "\"}\n",
           caps, getauxval(AT_HWCAP2), isar0, isar1, pfr0);
    return 0;
}
