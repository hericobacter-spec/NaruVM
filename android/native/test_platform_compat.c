/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform_compat.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    uint64_t caps=UINT64_C(0xefffffff),caps2=UINT64_C(0x273ff);
    unsigned bits[]={0,1,3,4,5,6,7,8,9,10,12,13,14,15,16,17,20,21,23,25,26,27,29};
    char why[160];int checks=0,failures=0;
#define CHECK(x) do { checks++;if(!(x)) { failures++;printf("FAIL line=%d reason=%s\n",__LINE__,why); } } while(0)
    CHECK(lab_platform_compatible(caps,caps2,4096,why,sizeof why));
    CHECK(lab_platform_compatible(caps,caps2,16384,why,sizeof why));
    CHECK(!lab_platform_compatible(caps,caps2,65536,why,sizeof why));
    for(unsigned i=0;i<sizeof bits/sizeof bits[0];++i) {
        CHECK(!lab_platform_compatible(caps & ~(UINT64_C(1)<<bits[i]),caps2,4096,why,sizeof why));
        CHECK(strstr(why,"missing CPU feature")!=NULL);
    }
    CHECK(!lab_platform_compatible(caps,caps2 & ~(UINT64_C(1)<<8),4096,why,sizeof why));
    CHECK(strstr(why,"frint")!=NULL);
    /* Features not advertised/copied must not become artificial requirements. */
    CHECK(lab_platform_compatible(caps & ~(UINT64_C(1)<<11) & ~(UINT64_C(1)<<22),caps2,4096,why,sizeof why));
    printf("PLATFORM_TEST_RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
