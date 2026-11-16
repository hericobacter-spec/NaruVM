/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HENGE_PLATFORM_COMPAT_H
#define HENGE_PLATFORM_COMPAT_H
#include <stdint.h>
#include <stddef.h>
/* Linux AArch64 HWCAP ABI. Covers the copied instructions advertised in
 * core/sysreg.c, Reims' LSE/RCpc build and unaligned LSE2 host accesses.
 * PAuth is handled by the translator rather than executed on the host. */
int lab_platform_compatible(uint64_t caps, uint64_t caps2, long page,
                            char *reason, size_t size);
int lab_platform_probe(void);
#endif
