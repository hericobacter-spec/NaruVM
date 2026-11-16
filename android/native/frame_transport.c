#include "frame_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SLOTS 3
#define MAX_SIDE 4096u
#define CAPACITY ((size_t)MAX_SIDE * MAX_SIDE * 4)
#define FRAME_MAGIC UINT64_C(0x31454d4152464848)
enum { FREE, WRITING, READY, READING };
typedef struct Slot {
    _Atomic uint32_t state;
    uint32_t width, height, reserved;
    _Atomic uint64_t sequence;
    uint64_t published_ns;
    uint8_t padding[32];
} Slot;
typedef struct Header {
    uint64_t magic, epoch_ms, bytes;
    _Atomic uint64_t published;
    uint8_t padding[32];
    Slot slots[SLOTS];
} Header;
_Static_assert(sizeof(Slot) == 64 && sizeof(Header) == 256, "Frame protocol layout");
struct LabFrames { Header *header; size_t bytes; int producer; uint64_t last_sequence; int held_slot; };
static const size_t file_bytes = sizeof(Header) + SLOTS * CAPACITY;
static uint8_t *pixels(Header *h, unsigned slot) { return (uint8_t *)h + sizeof(Header) + slot * CAPACITY; }

static LabFrames *map_file(int fd, int producer) {
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        (uint64_t)st.st_size != file_bytes) return NULL;
    Header *h = mmap(NULL, file_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (h == MAP_FAILED) return NULL;
    if (!atomic_is_lock_free(&h->published) || !atomic_is_lock_free(&h->slots[0].state)) {
        munmap(h, file_bytes); errno = ENOTSUP; return NULL;
    }
    LabFrames *f = calloc(1, sizeof *f);
    if (!f) { munmap(h, file_bytes); return NULL; }
    *f = (LabFrames){.header=h, .bytes=file_bytes, .producer=producer, .held_slot=-1}; return f;
}
LabFrames *lab_frames_create(const char *path, uint64_t epoch_ms) {
    char temporary[4096];
    int length = snprintf(temporary, sizeof temporary, "%s.new-%ld", path, (long)getpid());
    if (length < 0 || (size_t)length >= sizeof temporary) return NULL;
    int fd = open(temporary, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return NULL;
    LabFrames *f = NULL;
    if (ftruncate(fd, (off_t)file_bytes) == 0) f = map_file(fd, 1);
    close(fd);
    if (!f) { unlink(temporary); return NULL; }
    Header *h = f->header;
    memset(h, 0, sizeof *h);
    h->magic = FRAME_MAGIC; h->epoch_ms = epoch_ms; h->bytes = file_bytes;
    atomic_init(&h->published, 0);
    for (unsigned i = 0; i < SLOTS; ++i) { atomic_init(&h->slots[i].state, FREE); atomic_init(&h->slots[i].sequence, 0); }
    struct stat old;
    int exists = lstat(path, &old);
    if ((!exists && (!S_ISREG(old.st_mode) || old.st_uid != getuid())) ||
            (exists && errno != ENOENT) || rename(temporary, path)) {
        lab_frames_close(f); unlink(temporary); return NULL;
    }
    return f;
}
LabFrames *lab_frames_open(const char *path, uint64_t min_epoch_ms) {
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    LabFrames *f = map_file(fd, 0); close(fd);
    if (f && (f->header->magic != FRAME_MAGIC || f->header->bytes != file_bytes ||
              f->header->epoch_ms < min_epoch_ms)) { lab_frames_close(f); f = NULL; }
    return f;
}
void lab_frames_close(LabFrames *f) {
    if (f) {
        if (!f->producer && f->held_slot >= 0)
            atomic_store_explicit(&f->header->slots[f->held_slot].state, FREE, memory_order_release);
        munmap(f->header, f->bytes); free(f);
    }
}
int lab_frames_publish(LabFrames *f, const void *source, uint32_t width, uint32_t height) {
    if (!f || !f->producer || !source || !width || !height || width > MAX_SIDE || height > MAX_SIDE) return -1;
    Header *h = f->header;
    for (unsigned pass = 0; pass < 2; ++pass) for (unsigned i = 0; i < SLOTS; ++i) {
        uint32_t expected = pass ? READY : FREE;
        if (!atomic_compare_exchange_strong_explicit(&h->slots[i].state, &expected, WRITING,
                memory_order_acquire, memory_order_relaxed)) continue;
        Slot *s = &h->slots[i];
        memcpy(pixels(h, i), source, (size_t)width * height * 4);
        s->width = width; s->height = height;
        uint64_t n = atomic_load_explicit(&h->published, memory_order_relaxed) + 1;
        atomic_store_explicit(&s->sequence, n, memory_order_relaxed);
        struct timespec stamp; clock_gettime(CLOCK_MONOTONIC, &stamp);
        s->published_ns = (uint64_t)stamp.tv_sec * 1000000000 + stamp.tv_nsec;
        atomic_store_explicit(&s->state, READY, memory_order_release);
        atomic_store_explicit(&h->published, n, memory_order_release);
        return 1;
    }
    return 0; /* A slow/dead consumer never blocks the guest or producer. */
}
int lab_frames_take(LabFrames *f, LabFrameView *view) {
    if (!f || f->producer || !view || f->held_slot >= 0) return -1;
    Header *h = f->header;
    for (unsigned attempt = 0; attempt < SLOTS; ++attempt) {
        unsigned best = SLOTS; uint64_t newest = 0;
        for (unsigned i = 0; i < SLOTS; ++i) if (atomic_load_explicit(&h->slots[i].state, memory_order_acquire) == READY) {
            uint64_t n = atomic_load_explicit(&h->slots[i].sequence, memory_order_relaxed);
            if (n > newest) { best = i; newest = n; }
        }
        if (best == SLOTS) return 0;
        uint32_t expected = READY;
        Slot *s = &h->slots[best];
        if (!atomic_compare_exchange_strong_explicit(&s->state, &expected, READING,
                memory_order_acquire, memory_order_relaxed)) continue;
        if (!s->width || !s->height || s->width > MAX_SIDE || s->height > MAX_SIDE) {
            atomic_store_explicit(&s->state, FREE, memory_order_release); return -1;
        }
        uint64_t sequence = atomic_load_explicit(&s->sequence, memory_order_relaxed);
        if (sequence <= f->last_sequence) {
            atomic_store_explicit(&s->state, FREE, memory_order_release); continue;
        }
        *view = (LabFrameView){pixels(h, best), s->width, s->height, sequence, s->published_ns, best};
        f->last_sequence = sequence; f->held_slot = (int)best;
        return 1;
    }
    return 0;
}
void lab_frames_release(LabFrames *f, const LabFrameView *view) {
    if (f && !f->producer && view && view->slot < SLOTS && f->held_slot == (int)view->slot) {
        atomic_store_explicit(&f->header->slots[view->slot].state, FREE, memory_order_release);
        f->held_slot = -1;
    }
}
uint64_t lab_frames_published(const LabFrames *f) { return f ? atomic_load_explicit(&f->header->published, memory_order_acquire) : 0; }
