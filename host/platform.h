#ifndef HENGE_HOST_PLATFORM_H
#define HENGE_HOST_PLATFORM_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

static inline void hv_host_idle(void) {
#if defined(__APPLE__)
    __asm__ volatile("wfe" ::: "memory");
#else
    /* Android does not promise Apple's periodic event stream. Never wait forever. */
    __asm__ volatile("yield" ::: "memory");
#endif
}

static inline int hv_host_cond_wait_relative(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *delta) {
#if defined(__APPLE__)
    return pthread_cond_timedwait_relative_np(c, m, delta);
#else
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += delta->tv_sec;
    deadline.tv_nsec += delta->tv_nsec;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    return pthread_cond_timedwait(c, m, &deadline);
#endif
}

/* Keep the guest and existing statistics in a 24 MHz time domain on every host. */
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
extern int __ulock_wait(uint32_t, void *, uint64_t, uint32_t);
extern int __ulock_wake(uint32_t, void *, uint64_t);
static inline uint64_t hv_host_ticks(void) { return mach_absolute_time(); }
static inline uint32_t hv_host_thread_acquire(void) { return mach_thread_self(); }
static inline void hv_host_thread_release(uint32_t t) { if (t) mach_port_deallocate(mach_task_self(), t); }
static inline int hv_host_thread_running(uint32_t t) {
    if (!t) return 1;
    thread_basic_info_data_t info;
    mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
    return thread_info(t, THREAD_BASIC_INFO, (thread_info_t)&info, &n) == KERN_SUCCESS && info.run_state == TH_STATE_RUNNING;
}
static inline double hv_host_thread_busy(void) {
    thread_basic_info_data_t info;
    mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
    mach_port_t t = mach_thread_self();
    int ok = thread_info(t, THREAD_BASIC_INFO, (thread_info_t)&info, &n) == KERN_SUCCESS;
    mach_port_deallocate(mach_task_self(), t);
    return ok ? info.user_time.seconds + info.user_time.microseconds / 1e6 +
                info.system_time.seconds + info.system_time.microseconds / 1e6 : -1;
}
static inline void hv_host_wait(void *p, uint32_t value, uint32_t us) { __ulock_wait(1u | 0x1000000u, p, value, us); }
static inline void hv_host_wake(void *p) { __ulock_wake(1u | 0x100u | 0x1000000u, p, 0); }
#else
#include <limits.h>
#include <linux/futex.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
static inline uint64_t hv_host_ticks(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 24000000ull + (uint64_t)t.tv_nsec * 3 / 125;
}
static inline uint32_t hv_host_thread_acquire(void) { return (uint32_t)syscall(SYS_gettid); }
static inline void hv_host_thread_release(uint32_t t) { (void)t; }
static inline int hv_host_thread_running(uint32_t t) {
    if (!t) return 1;
    char path[80], line[1024];
    snprintf(path, sizeof path, "/proc/self/task/%u/stat", t);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char *got = fgets(line, sizeof line, f);
    fclose(f);
    char *end = got ? strrchr(line, ')') : NULL;
    return end && end[1] == ' ' && end[2] == 'R';
}
static inline double hv_host_thread_busy(void) {
    struct timespec t;
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0 ? t.tv_sec + t.tv_nsec / 1e9 : -1;
}
static inline void hv_host_wait(void *p, uint32_t value, uint32_t us) {
    struct timespec timeout = {us / 1000000, (us % 1000000) * 1000};
    syscall(SYS_futex, p, FUTEX_WAIT_PRIVATE, value, &timeout, NULL, 0);
}
static inline void hv_host_wake(void *p) { syscall(SYS_futex, p, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0); }
#endif
#endif
