#ifndef HENGE_CPU_PERFORMANCE_H
#define HENGE_CPU_PERFORMANCE_H
#include <sched.h>
int lab_cpu_fast_mask(cpu_set_t *mask);
int lab_cpu_performance(unsigned index);
#endif
