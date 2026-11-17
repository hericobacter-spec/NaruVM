/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Software-renderer child-process shim. Never adds an ISA capability. */
#define _GNU_SOURCE
#include "cpu_caps_policy.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/auxv.h>
#include <unistd.h>

static pthread_once_t once = PTHREAD_ONCE_INIT;
static unsigned long (*original)(unsigned long);
static unsigned long filtered;

static char *hex(char *out, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) *out++ = digits[(value >> shift) & 15];
    return out;
}
static void initialize(void) {
    original = (unsigned long (*)(unsigned long))dlsym(RTLD_NEXT, "getauxval");
    if (!original) return;
    unsigned long first = original(AT_HWCAP), second = original(AT_HWCAP2);
    filtered = (unsigned long)naru_cpu_caps2(first, second);
    if (filtered != second) {
        char text[192], *out = text;
        const char *label = "NARU_CPU_CAPS inconsistent_optional_features_removed hwcap2=";
        while (*label) *out++ = *label++;
        out = hex(out, second);
        label = " filtered=";
        while (*label) *out++ = *label++;
        out = hex(out, filtered); *out++ = '\n';
        (void)write(STDERR_FILENO, text, (size_t)(out - text));
    }
}
unsigned long getauxval(unsigned long type) {
    int saved = errno;
    pthread_once(&once, initialize);
    if (!original) { errno = ENOENT; return 0; }
    errno = saved;
    return type == AT_HWCAP2 ? filtered : original(type);
}
