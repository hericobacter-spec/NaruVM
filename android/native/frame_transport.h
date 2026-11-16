#ifndef HENGE_FRAME_TRANSPORT_H
#define HENGE_FRAME_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>

typedef struct LabFrames LabFrames;
typedef struct LabFrameView {
    const uint8_t *bgra;
    uint32_t width, height;
    uint64_t sequence;
    uint64_t published_ns;
    unsigned slot;
} LabFrameView;
LabFrames *lab_frames_create(const char *path, uint64_t epoch_ms);
LabFrames *lab_frames_open(const char *path, uint64_t min_epoch_ms);
void lab_frames_close(LabFrames *f);
/* Single producer/consumer, nonblocking, complete frames only. Pixels are BGRA8. */
int lab_frames_publish(LabFrames *f, const void *pixels, uint32_t width, uint32_t height);
int lab_frames_take(LabFrames *f, LabFrameView *view);
void lab_frames_release(LabFrames *f, const LabFrameView *view);
uint64_t lab_frames_published(const LabFrames *f);
#endif
