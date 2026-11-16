#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "cpu_performance.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Select the high-capacity cluster from the caller's existing allowed CPUs.
 * This changes only a vCPU thread's affinity, never clocks or thermal policy. */
int lab_cpu_fast_mask(cpu_set_t *mask) {
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof allowed, &allowed)) return -1;
    unsigned capacity[CPU_SETSIZE] = {0}, maximum = 0;
    for (unsigned i = 0; i < CPU_SETSIZE; i++) {
        if (!CPU_ISSET(i, &allowed)) continue;
        char path[128];
        snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu%u/cpu_capacity",i);
        FILE *file = fopen(path,"r");
        if (!file) continue;
        unsigned value = 0;
        int read = fscanf(file,"%u",&value); fclose(file);
        if (read != 1 || value > 1000000) continue;
        capacity[i] = value;
        if (value > maximum) maximum = value;
    }
    CPU_ZERO(mask);
    if (!maximum) return 0;
    int count = 0;
    for (unsigned i = 0; i < CPU_SETSIZE; i++) {
        if (capacity[i] && capacity[i] * 4 >= maximum * 3) {
            CPU_SET(i,mask); count++;
        }
    }
    return count;
}

int lab_cpu_performance(unsigned index) {
    cpu_set_t fast;
    int count = lab_cpu_fast_mask(&fast);
    if (count < 2) {
        printf("VCPU_AFFINITY cpu=%u applied=0 eligible=%d errno=%d\n",index,count,count < 0 ? errno : 0);
        return count < 0 ? -1 : 0;
    }
    if (sched_setaffinity(0,sizeof fast,&fast)) {
        printf("VCPU_AFFINITY cpu=%u applied=0 eligible=%d errno=%d\n",index,count,errno);
        return -1;
    }
    printf("VCPU_AFFINITY cpu=%u applied=1 eligible=%d current=%d\n",index,count,sched_getcpu());
    return count;
}
