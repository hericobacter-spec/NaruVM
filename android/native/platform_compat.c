/* SPDX-License-Identifier: GPL-2.0-or-later
 * 2026-11-16 Android port: refuse an incompatible ISA before copied guest
 * instructions or dependency code can fault. No device/model allowlist. */
#include "platform_compat.h"
#include <stdio.h>
#include <string.h>
#ifdef __ANDROID__
#include <sys/auxv.h>
#include <unistd.h>
#include "jit_region.h"
#endif

int lab_platform_compatible(uint64_t caps, uint64_t caps2, long page,
                            char *reason, size_t size) {
    static const struct { unsigned bit; const char *name; } required[] = {
        {0,"fp"},{1,"asimd"},{3,"aes"},{4,"pmull"},{5,"sha1"},{6,"sha2"},
        {7,"crc32"},{8,"atomics"},{9,"fphp"},{10,"asimdhp"},{12,"asimdrdm"},
        {13,"jscvt"},{14,"fcma"},{15,"lrcpc"},{16,"dcpop"},{17,"sha3"},
        {20,"asimddp"},{21,"sha512"},{23,"asimdfhm"},{25,"uscat"},
        {26,"ilrcpc"},{27,"flagm"},{29,"sb"}
    };
    if (page != 4096 && page != 16384) {
        snprintf(reason,size,"unsupported host page size %ld (4K/16K build contract)",page); return 0;
    }
    for (unsigned i=0;i<sizeof required/sizeof required[0];++i) {
        if (!(caps & (UINT64_C(1)<<required[i].bit))) {
            snprintf(reason,size,"missing CPU feature %s",required[i].name); return 0;
        }
    }
    if (!(caps2 & (UINT64_C(1)<<8))) {
        snprintf(reason,size,"missing CPU feature frint"); return 0;
    }
    snprintf(reason,size,"advertised_guest_ISA_and_runtime_requirements"); return 1;
}

int lab_platform_probe(void) {
#ifdef __ANDROID__
    char reason[160];
    int ok=lab_platform_compatible(getauxval(AT_HWCAP),getauxval(AT_HWCAP2),
                                   sysconf(_SC_PAGESIZE),reason,sizeof reason);
    if (ok) {
        HvJitRegion region;
        if (hv_jit_region_open(&region,16384)) {
            snprintf(reason,sizeof reason,"JIT RW/RX mapping unavailable"); ok=0;
        } else hv_jit_region_close(&region);
    }
    printf("PLATFORM_RESULT eligible=%d reason=%s guest=none disk=none\n",ok,reason);
    return ok ? 0 : 77;
#else
    return 77;
#endif
}
