/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NARU_CPU_CAPS_POLICY_H
#define NARU_CPU_CAPS_POLICY_H
#include <stdint.h>

/* Linux AArch64 UAPI bit positions. Derived SVE/SME features cannot be used
 * without their base feature, even if an inconsistent virtual CPU lists them. */
#define NARU_SVE_BASE (UINT64_C(1) << 22)
#define NARU_SME_BASE (UINT64_C(1) << 23)
#define NARU_SVE_DERIVED (UINT64_C(0x1e7e) | (UINT64_C(1) << 33) | \
                        (UINT64_C(1) << 36) | (UINT64_C(1) << 45))
#define NARU_SME_DERIVED ((UINT64_C(0x7f) << 24) | (UINT64_C(0x3f) << 37) | \
                        (UINT64_C(0x3f) << 57))

static inline uint64_t naru_cpu_caps2(uint64_t first, uint64_t second) {
    if (!(first & NARU_SVE_BASE)) second &= ~NARU_SVE_DERIVED;
    if (!(second & NARU_SME_BASE)) second &= ~NARU_SME_DERIVED;
    return second;
}
#endif
