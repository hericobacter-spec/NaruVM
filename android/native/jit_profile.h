#ifndef HENGE_ANDROID_JIT_PROFILE_H
#define HENGE_ANDROID_JIT_PROFILE_H

#include "machine.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct LabJitSample {
    uint64_t pc, weight;
    const HvBlock *block;
    uint32_t offset;
    int cold;
} LabJitSample;

static inline int lab_jit_sample_compare(const void *a, const void *b) {
    uint64_t x = ((const LabJitSample *)a)->pc, y = ((const LabJitSample *)b)->pc;
    return x < y ? -1 : x != y;
}

/* Opt-in address metadata only. The caller must hold the translation lock or
 * have joined the vCPUs; reject recycled addresses from another generation. */
static inline int lab_jit_profile_write(HvMachine *m, FILE *in, FILE *out, unsigned pid) {
    unsigned requested_pid, generation, count;
    uint64_t nonce;
    char magic[32];
    if (fscanf(in, "%31s %u %u %" SCNu64 " %u", magic, &requested_pid,
               &generation, &nonce, &count) != 5 ||
        strcmp(magic, "HENGE_JIT_PROFILE_V1") || count > 4096) {
        fprintf(out, "JIT_PROFILE_ERROR invalid_request\n");
        return -1;
    }
    if (requested_pid != pid || generation != m->jit_gen) {
        fprintf(out, "JIT_PROFILE_ERROR stale_generation pid=%u generation=%u nonce=%" PRIu64 "\n",
                pid, m->jit_gen, nonce);
        return -1;
    }
    if (!m->jit_spans) {
        fprintf(out, "JIT_PROFILE_ERROR spans_disabled\n");
        return -1;
    }
    LabJitSample samples[4096] = {0};
    for (unsigned i = 0; i < count; ++i) {
        if (fscanf(in, "%" SCNx64 " %" SCNu64, &samples[i].pc, &samples[i].weight) != 2) {
            fprintf(out, "JIT_PROFILE_ERROR truncated_request\n");
            return -1;
        }
    }
    char extra;
    if (fscanf(in, " %c", &extra) == 1) {
        fprintf(out, "JIT_PROFILE_ERROR trailing_request\n");
        return -1;
    }
    qsort(samples, count, sizeof *samples, lab_jit_sample_compare);
    /* Search the bounded sample set for each exact range, not the unsorted
     * block array. This also distinguishes cold helpers from hot execution. */
    for (int i = 0; i < m->nblocks; ++i) {
        HvBlock *b = &m->blocks[i];
        if (b->jit_gen != generation) {
            fprintf(out, "JIT_PROFILE_ERROR block_generation\n");
            return -1;
        }
        HvJitSpan *span = &m->jit_spans[i];
        for (int cold = 0; cold < 2; ++cold) {
            uintptr_t base = (uintptr_t)(cold ? span->cold : b->code);
            uintptr_t end = base + (uint64_t)(cold ? span->cold_words : span->hot_words) * 4;
            unsigned lo = 0, hi = count;
            while (lo < hi) {
                unsigned mid = (lo + hi) / 2;
                if (samples[mid].pc < base) lo = mid + 1;
                else hi = mid;
            }
            for (unsigned k = lo; k < count && samples[k].pc < end; ++k) {
                if (samples[k].block) {
                    fprintf(out, "JIT_PROFILE_ERROR overlapping_ranges\n");
                    return -1;
                }
                samples[k].block = b;
                samples[k].offset = (samples[k].pc - base) / 4;
                samples[k].cold = cold;
            }
        }
    }
    fprintf(out, "JIT_PROFILE_V1 pid=%u generation=%u nonce=%" PRIu64 " count=%u\n",
            pid, generation, nonce, count);
    for (unsigned i = 0; i < count; ++i) {
        LabJitSample *s = &samples[i];
        if (!s->block) fprintf(out, "outside 0x%" PRIx64 " %" PRIu64 "\n", s->pc, s->weight);
        else fprintf(out, "block 0x%" PRIx64 " 0x%" PRIx64 " %u %u %u %s %" PRIu64 "\n",
            s->pc, s->block->va, s->block->flags, s->block->ninsns,
            s->offset, s->cold ? "cold" : "hot", s->weight);
    }
    fprintf(out, "JIT_PROFILE_COMPLETE\n");
    return ferror(out) ? -1 : 0;
}

static inline void lab_jit_profile_finish(HvMachine *m) {
    if (!getenv("HENGE_CPU_PROFILE") || access("jit-profile-request.txt", R_OK) ||
        pthread_mutex_trylock(&m->tr_mu)) return;
    FILE *in = fopen("jit-profile-request.txt", "r");
    if (!in) { pthread_mutex_unlock(&m->tr_mu); return; }
    FILE *out = fopen("jit-profile-result.tmp", "w");
    if (!out) { fclose(in); pthread_mutex_unlock(&m->tr_mu); return; }
    int result = lab_jit_profile_write(m, in, out, (unsigned)getpid());
    unsigned generation = m->jit_gen;
    fclose(in);
    if (fclose(out)) result = -1;
    if (rename("jit-profile-result.tmp", "jit-profile-result.txt")) result = -1;
    else unlink("jit-profile-request.txt");
    pthread_mutex_unlock(&m->tr_mu);
    printf("JIT_PROFILE_RESULT status=%d generation=%u\n", result, generation);
}
#endif
