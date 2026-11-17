/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_caps_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    /* Actual inconsistent Android35 ARM64 emulator sample. */
    assert(naru_cpu_caps2(UINT64_C(0xefb3ffff), UINT64_C(0x7f33f105383)) == UINT64_C(0x100104181));
    assert(naru_cpu_caps2(0, 0) == 0);
    /* Do not suppress real SVE, SME, or unrelated NEON/crypto/BF16 features. */
    assert(naru_cpu_caps2(NARU_SVE_BASE, UINT64_MAX) == UINT64_MAX);
    assert(naru_cpu_caps2(0, UINT64_C(1) << 14) == (UINT64_C(1) << 14));
    for (unsigned bit = 0; bit < 64; ++bit) {
        uint64_t value = UINT64_C(1) << bit;
        assert((naru_cpu_caps2(0, value) & ~value) == 0);
        assert(naru_cpu_caps2(NARU_SVE_BASE, value | NARU_SME_BASE) == (value | NARU_SME_BASE));
    }
    puts("CPU_CAPS_POLICY_RESULT checks=132 failures=0 scope=feature-policy");
    return 0;
}
