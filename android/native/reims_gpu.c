/* The Android host adapter uses the public Reims C API. Guest RAM is shared,
 * IRQ callbacks publish pending edges, and only Henge's device thread touches its GIC. */
#include "reims_gpu.h"
#include "vmapple.h"
#include "frame_transport.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(HENGE_ANDROID_REIMS)
#include "reims_vgpu.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/auxv.h>
#include <time.h>
#include <unistd.h>

#define API_FUNCTIONS(X) \
    X(rvg_log_set_handler) X(rvg_device_create) X(rvg_device_release) \
    X(rvg_device_stop) X(rvg_device_mmio_read) X(rvg_device_mmio_write) \
    X(rvg_iosurface_device_create) X(rvg_iosurface_device_release) \
    X(rvg_iosurface_device_mmio_read) X(rvg_iosurface_device_mmio_write) \
    X(rvg_display_create) X(rvg_display_release) X(rvg_display_set_modes) \
    X(rvg_display_encode_frame) X(rvg_display_present_counts) \
    X(rvg_display_cursor_position) \
    X(rvg_frame_target_create) X(rvg_frame_target_release) X(rvg_frame_target_read_pixels) \
    X(rvg_frame_commit) X(rvg_frame_wait) X(rvg_frame_release)

enum { DIRTY_SURFACE = 1, DIRTY_CURSOR = 2 };

struct HvAndroidReims {
    HvVmapple *board;
    void *library;
    RvgDevice *device;
    RvgIOSurfaceDevice *iosurface;
    RvgDisplay *display;
    RvgFrameTarget *target;
    pthread_t presenter;
    int presenter_started;
    atomic_bool stopping;
    atomic_uint dirty;
    atomic_uint pending, callbacks_live;
    atomic_uint_fast64_t mode, irq_count, frame_count, saved_count;
    uint64_t gfx_reads, gfx_writes, ios_reads, ios_writes;
    uint32_t *pixels, *previous_pixels, *surface_pixels, width, height;
    int previous_valid, surface_valid;
    LabFrames *live_frames;
    uint64_t last_snapshot_us;
    uint64_t last_metrics_us;
    atomic_uint_fast64_t capture_us, capture_max_us;
    atomic_uint_fast64_t encode_us, wait_us, read_us, hash_us, publish_us;
    atomic_uint_fast64_t surface_count, cursor_count, surface_us, cursor_us;
    pthread_mutex_t cursor_lock;
    uint8_t *cursor_rgba;
    uint32_t cursor_width, cursor_height, hot_x, hot_y;
    int cursor_visible;
    uint64_t cursor_updates;
    char path[4096];
#define DECLARE(name) __typeof__(&name) name;
    API_FUNCTIONS(DECLARE)
#undef DECLARE
};
static uint64_t microseconds(clockid_t clock) {
    struct timespec t; clock_gettime(clock, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void log_line(void *ctx, RvgLogLevel level, const char *target, size_t tn, const char *message, size_t mn) {
    (void)ctx;
    fprintf(stderr, "reims[%u] %.*s: %.*s\n", level, (int)tn, target, (int)mn, message);
}
static void callbacks_end(void *ctx) {
    HvAndroidReims *g = ctx;
    atomic_fetch_sub(&g->callbacks_live, 1);
}
static void gfx_irq(void *ctx, uint32_t vector) {
    HvAndroidReims *g = ctx;
    if (vector != 0) { fprintf(stderr, "REIMS_UNEXPECTED_GPU_VECTOR %u\n", vector); abort(); }
    atomic_fetch_add(&g->irq_count, 1);
    atomic_fetch_or(&g->pending, 1);
}
static void ios_irq(void *ctx, uint32_t vector) {
    HvAndroidReims *g = ctx;
    if (vector != 0) { fprintf(stderr, "REIMS_UNEXPECTED_IOSURFACE_VECTOR %u\n", vector); abort(); }
    atomic_fetch_add(&g->irq_count, 1);
    atomic_fetch_or(&g->pending, 2);
}
static void mode_change(void *ctx, uint16_t width, uint16_t height, uint32_t format) {
    HvAndroidReims *g = ctx;
    atomic_store(&g->mode, (uint64_t)width | ((uint64_t)height << 32));
    atomic_fetch_or(&g->dirty, DIRTY_SURFACE);
    fprintf(stderr, "REIMS_GUEST_MODE width=%u height=%u format=0x%x\n", width, height, format);
}
static void new_frame(void *ctx) {
    HvAndroidReims *g = ctx;
    atomic_fetch_or(&g->dirty, DIRTY_SURFACE);
}
static void cursor_glyph(void *ctx, const RvgCursorGlyph *glyph) {
    HvAndroidReims *g = ctx;
    if (glyph->width > 256 || glyph->height > 256 ||
        glyph->rgba_length != (size_t)glyph->width * glyph->height * 4) {
        fprintf(stderr, "REIMS_CURSOR_GLYPH_INVALID\n"); hv_machine_halt(g->board->m, HV_HALT_HOST); return;
    }
    size_t bytes = glyph->rgba_length;
    uint8_t *copy = bytes ? malloc(bytes) : NULL;
    if (bytes && !copy) { hv_machine_halt(g->board->m, HV_HALT_HOST); return; }
    if (bytes) memcpy(copy, glyph->rgba, bytes);
    pthread_mutex_lock(&g->cursor_lock);
    free(g->cursor_rgba); g->cursor_rgba = copy;
    g->cursor_width = glyph->width; g->cursor_height = glyph->height;
    g->hot_x = glyph->hot_spot.x; g->hot_y = glyph->hot_spot.y;
    if (++g->cursor_updates <= 4) printf("REIMS_CURSOR_GLYPH width=%u height=%u hot_x=%u hot_y=%u\n", glyph->width, glyph->height, glyph->hot_spot.x, glyph->hot_spot.y);
    pthread_mutex_unlock(&g->cursor_lock);
    atomic_fetch_or(&g->dirty, DIRTY_CURSOR);
}
static void cursor_show(void *ctx, uint8_t show) {
    HvAndroidReims *g = ctx;
    pthread_mutex_lock(&g->cursor_lock); g->cursor_visible = show != 0; pthread_mutex_unlock(&g->cursor_lock);
    atomic_fetch_or(&g->dirty, DIRTY_CURSOR);
}
static void cursor_move(void *ctx) {
    HvAndroidReims *g = ctx;
    atomic_fetch_or(&g->dirty, DIRTY_CURSOR);
}
static void composite_cursor(HvAndroidReims *g) {
    RvgDisplayCoord pos = g->rvg_display_cursor_position(g->display);
    if (pos.x == UINT16_MAX || pos.y == UINT16_MAX) return;
    pthread_mutex_lock(&g->cursor_lock);
    if (g->cursor_visible && g->cursor_rgba) {
        for (uint32_t y = 0; y < g->cursor_height; ++y) for (uint32_t x = 0; x < g->cursor_width; ++x) {
            int dx = (int)pos.x - (int)g->hot_x + (int)x, dy = (int)pos.y - (int)g->hot_y + (int)y;
            if (dx < 0 || dy < 0 || (uint32_t)dx >= g->width || (uint32_t)dy >= g->height) continue;
            const uint8_t *src = g->cursor_rgba + ((size_t)y * g->cursor_width + x) * 4;
            uint8_t *dst = (uint8_t *)&g->pixels[(size_t)dy * g->width + dx];
            unsigned inv = 255 - src[3];
            /* The upstream window pipeline uses premultiplied One/OneMinusSourceAlpha. */
            for (unsigned c = 0; c < 3; ++c) {
                unsigned value = src[2 - c] + (dst[c] * inv + 127) / 255;
                dst[c] = value > 255 ? 255 : (uint8_t)value;
            }
            dst[3] = (uint8_t)(src[3] + (dst[3] * inv + 127) / 255);
        }
    }
    pthread_mutex_unlock(&g->cursor_lock);
}
static void le32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static int save_frame(HvAndroidReims *g, size_t bytes) {
    char temp[4120];
    if (snprintf(temp, sizeof temp, "%s.tmp", g->path) >= sizeof temp) return -1;
    FILE *f = fopen(temp, "wb");
    if (!f) return -1;
    uint8_t header[54] = {'B', 'M'};
    le32(header + 2, 54 + (uint32_t)bytes); le32(header + 10, 54); le32(header + 14, 40);
    le32(header + 18, g->width); le32(header + 22, -(int32_t)g->height);
    header[26] = 1; header[28] = 32; le32(header + 34, (uint32_t)bytes);
    int failed = fwrite(header, 1, sizeof header, f) != sizeof header || fwrite(g->pixels, 1, bytes, f) != bytes;
    if (fflush(f) || fsync(fileno(f))) failed = 1;
    if (fclose(f)) failed = 1;
    if (failed || rename(temp, g->path)) { unlink(temp); return -1; }
    uint64_t n = atomic_fetch_add(&g->saved_count, 1) + 1;
    if (n == 1 || n % 10 == 0) {
        uint64_t hash = 14695981039346656037ull;
        const uint8_t *p = (const uint8_t *)g->pixels;
        for (size_t i = 0; i < bytes; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
        printf("REIMS_GUEST_FRAME saved=%" PRIu64 " width=%u height=%u hash=%016" PRIx64 " path=%s\n", n, g->width, g->height, hash, g->path);
    }
    return 0;
}
static RvgStatus capture(HvAndroidReims *g, unsigned dirty) {
    uint64_t started = microseconds(CLOCK_MONOTONIC);
    uint64_t mode = atomic_load(&g->mode);
    uint32_t width = (uint32_t)mode, height = (uint32_t)(mode >> 32);
    if (!width || !height) return RVG_OK;
    if (width > 4096 || height > 4096) return RVG_ERR_INVALID_ARGUMENT;
    if (width != g->width || height != g->height) {
        RvgFrameTarget *target = NULL;
        RvgFrameTargetConfig config = {.struct_size = sizeof config, .width = width, .height = height,
            .format = RVG_PIXEL_FORMAT_BGRA8_UNORM};
        RvgStatus status = g->rvg_frame_target_create(g->device, &config, &target);
        if (status != RVG_OK) return status;
        uint32_t *pixels = malloc((size_t)width * height * 4);
        uint32_t *previous = malloc((size_t)width * height * 4);
        uint32_t *surface = malloc((size_t)width * height * 4);
        if (!pixels || !previous || !surface) { free(pixels); free(previous); free(surface); g->rvg_frame_target_release(target); return RVG_ERR_GPU; }
        if (g->target) g->rvg_frame_target_release(g->target);
        free(g->pixels); g->pixels = pixels; g->target = target;
        free(g->previous_pixels); g->previous_pixels = previous;
        free(g->surface_pixels); g->surface_pixels = surface;
        g->width = width; g->height = height; g->previous_valid = g->surface_valid = 0;
    }
    uint64_t phase = microseconds(CLOCK_MONOTONIC);
    uint64_t mark;
    size_t bytes = (size_t)g->width * g->height * 4;
    int surface = (dirty & DIRTY_SURFACE) || !g->surface_valid;
    if (surface) {
        RvgFrame *frame = NULL;
        RvgStatus status = g->rvg_display_encode_frame(g->display, g->target, NULL, &frame);
        mark = microseconds(CLOCK_MONOTONIC); atomic_fetch_add(&g->encode_us, mark-phase); phase=mark;
        if (status == RVG_OK) status = g->rvg_frame_commit(frame);
        if (status == RVG_OK) status = g->rvg_frame_wait(frame);
        mark = microseconds(CLOCK_MONOTONIC); atomic_fetch_add(&g->wait_us, mark-phase); phase=mark;
        if (status == RVG_OK) status = g->rvg_frame_target_read_pixels(g->target, g->surface_pixels, g->width * 4, bytes);
        if (frame) g->rvg_frame_release(frame);
        mark = microseconds(CLOCK_MONOTONIC); atomic_fetch_add(&g->read_us, mark-phase); phase=mark;
        if (status != RVG_OK) return status;
        g->surface_valid = 1;
    }
    /* Cursor callbacks do not present a new guest surface. Start from the pristine
     * last readback so movement/hide never leaves a cursor trail or needs a GPU wait. */
    if (g->previous_valid) {
        /* Keep the last composition for comparison and reuse its old comparison
         * storage as scratch. Publishing copies synchronously, so neither buffer
         * is borrowed by the transport. This saves one full-frame copy per change. */
        uint32_t *scratch = g->previous_pixels;
        g->previous_pixels = g->pixels;
        g->pixels = scratch;
    }
    memcpy(g->pixels, g->surface_pixels, bytes);
    composite_cursor(g);
    atomic_fetch_add(&g->frame_count, 1);
    int changed = !g->previous_valid || memcmp(g->pixels, g->previous_pixels, bytes) != 0;
    mark = microseconds(CLOCK_MONOTONIC); atomic_fetch_add(&g->hash_us, mark-phase); phase=mark;
    if (changed) {
        if (lab_frames_publish(g->live_frames, g->pixels, g->width, g->height) < 0) return RVG_ERR_GPU;
        g->previous_valid = 1;
        uint64_t now = microseconds(CLOCK_MONOTONIC);
        if (!g->last_snapshot_us || (getenv("HENGE_REIMS_SNAPSHOTS") && now - g->last_snapshot_us >= 5000000)) {
            if (save_frame(g, bytes)) return RVG_ERR_GPU;
            g->last_snapshot_us = now;
        }
    }
    uint64_t elapsed = microseconds(CLOCK_MONOTONIC) - started;
    atomic_fetch_add(&g->publish_us, microseconds(CLOCK_MONOTONIC)-phase);
    atomic_fetch_add(&g->capture_us, elapsed);
    atomic_fetch_add(surface ? &g->surface_count : &g->cursor_count, 1);
    atomic_fetch_add(surface ? &g->surface_us : &g->cursor_us, elapsed);
    if (elapsed > atomic_load(&g->capture_max_us)) atomic_store(&g->capture_max_us, elapsed);
    uint64_t now = microseconds(CLOCK_MONOTONIC);
    if (!g->last_metrics_us) g->last_metrics_us = now;
    else if (now - g->last_metrics_us >= 60000000) {
        uint64_t surfaces = atomic_load(&g->surface_count), cursors = atomic_load(&g->cursor_count);
        printf("REIMS_SCANOUT_CACHE_SAMPLE surfaces=%" PRIu64 " cursor_only=%" PRIu64 " surface_mean_us=%" PRIu64 " cursor_mean_us=%" PRIu64 "\n",
            surfaces, cursors, surfaces ? atomic_load(&g->surface_us)/surfaces : 0, cursors ? atomic_load(&g->cursor_us)/cursors : 0);
        g->last_metrics_us = now;
    }
    return RVG_OK;
}
static void *presenter(void *ctx) {
    HvAndroidReims *g = ctx;
    while (!atomic_load(&g->stopping)) {
        uint64_t began = microseconds(CLOCK_MONOTONIC);
        unsigned dirty = atomic_exchange(&g->dirty, 0);
        if (dirty) {
            RvgStatus status = capture(g, dirty);
            if (status != RVG_OK) {
                fprintf(stderr, "REIMS_CAPTURE_ERROR status=%u\n", status);
                hv_machine_halt(g->board->m, HV_HALT_HOST);
                break;
            }
        }
        /* Surface work is capped at 30Hz; cached cursor work is polled at 60Hz.
         * Guest command completion/fences still pass through the public Reims API. */
        uint64_t spent = microseconds(CLOCK_MONOTONIC) - began;
        uint64_t period = dirty & DIRTY_SURFACE ? 33333 : 16667;
        if (spent < period) usleep((useconds_t)(period - spent));
    }
    return NULL;
}
static void bad_access(const char *name, uint64_t off, unsigned size) {
    fprintf(stderr, "REIMS_MMIO_ACCESS_ERROR device=%s off=0x%" PRIx64 " size=%u\n", name, off, size);
    abort();
}
static uint64_t gfx_read(void *ctx, uint64_t off, unsigned size) {
    HvAndroidReims *g = ctx;
    if (size != 4 || (off & 3)) bad_access("gpu", off, size);
    g->gfx_reads++;
    return g->rvg_device_mmio_read(g->device, off);
}
static void gfx_write(void *ctx, uint64_t off, uint64_t v, unsigned size) {
    HvAndroidReims *g = ctx;
    if (size != 4 || (off & 3)) bad_access("gpu", off, size);
    if (g->gfx_writes++ < 40) printf("REIMS_GUEST_MMIO off=0x%" PRIx64 " value=0x%" PRIx64 "\n", off, v);
    g->rvg_device_mmio_write(g->device, off, (uint32_t)v);
}
static uint64_t ios_read(void *ctx, uint64_t off, unsigned size) {
    HvAndroidReims *g = ctx;
    if ((size != 4 && size != 8) || (off & 7)) bad_access("iosurface", off, size);
    g->ios_reads++;
    uint64_t v = g->rvg_iosurface_device_mmio_read(g->iosurface, off);
    return size == 4 ? (uint32_t)v : v;
}
static void ios_write(void *ctx, uint64_t off, uint64_t v, unsigned size) {
    HvAndroidReims *g = ctx;
    if ((size != 4 && size != 8) || (off & 7)) bad_access("iosurface", off, size);
    if (g->ios_writes++ < 40) printf("REIMS_GUEST_IOSURFACE_MMIO off=0x%" PRIx64 " value=0x%" PRIx64 " size=%u\n", off, v, size);
    g->rvg_iosurface_device_mmio_write(g->iosurface, off, size == 4 ? (uint32_t)v : v);
}
int hv_android_reims_pointer(HvAndroidReims *g, unsigned *width, unsigned *height, int *x, int *y) {
    if (!g) return 0;
    uint64_t mode=atomic_load(&g->mode);
    RvgDisplayCoord pos=g->rvg_display_cursor_position(g->display);
    *width=(uint32_t)mode; *height=(uint32_t)(mode>>32); *x=pos.x; *y=pos.y;
    return *width && *height && pos.x!=UINT16_MAX && pos.y!=UINT16_MAX;
}
void hv_android_reims_poll(HvAndroidReims *g) {
    if (!g) return;
    unsigned edges = atomic_exchange(&g->pending, 0);
    if (edges & 1) hv_gicv3_pend(g->board->gic, 49);
    if (edges & 2) hv_gicv3_pend(g->board->gic, 48);
}
void hv_android_reims_report(HvAndroidReims *g) {
    if (!g) return;
    uint64_t guest = 0, host = 0;
    g->rvg_display_present_counts(g->display, &guest, &host);
    printf("REIMS_BOARD_STATS gfx_reads=%" PRIu64 " gfx_writes=%" PRIu64 " ios_reads=%" PRIu64 " ios_writes=%" PRIu64
        " interrupts=%" PRIuFAST64 " frames=%" PRIuFAST64 " saved=%" PRIuFAST64 " guest_presents=%" PRIu64 " host_presents=%" PRIu64 "\n",
        g->gfx_reads, g->gfx_writes, g->ios_reads, g->ios_writes, atomic_load(&g->irq_count),
        atomic_load(&g->frame_count), atomic_load(&g->saved_count), guest, host);
}
void hv_android_reims_destroy(HvAndroidReims *g) {
    if (!g) return;
    atomic_store(&g->stopping, true);
    if (g->presenter_started) pthread_join(g->presenter, NULL);
    uint64_t count = atomic_load(&g->frame_count);
    printf("REIMS_TRANSPORT_RESULT published=%" PRIu64 " captured=%" PRIu64 " capture_mean_us=%" PRIuFAST64
        " capture_max_us=%" PRIuFAST64 " surface_period_us=33333 cursor_period_us=16667\n", lab_frames_published(g->live_frames), count,
        count ? atomic_load(&g->capture_us) / count : 0, atomic_load(&g->capture_max_us));
    if (count) printf("REIMS_CAPTURE_PHASE_MEAN_US encode=%" PRIuFAST64 " submit_wait=%" PRIuFAST64 " read=%" PRIuFAST64
        " cursor_compare=%" PRIuFAST64 " publish_snapshot=%" PRIuFAST64 "\n", atomic_load(&g->encode_us)/count,
        atomic_load(&g->wait_us)/count, atomic_load(&g->read_us)/count, atomic_load(&g->hash_us)/count, atomic_load(&g->publish_us)/count);
    uint64_t surfaces = atomic_load(&g->surface_count), cursors = atomic_load(&g->cursor_count);
    printf("REIMS_SCANOUT_CACHE surfaces=%" PRIu64 " cursor_only=%" PRIu64 " surface_mean_us=%" PRIu64 " cursor_mean_us=%" PRIu64 "\n",
        surfaces, cursors, surfaces ? atomic_load(&g->surface_us)/surfaces : 0, cursors ? atomic_load(&g->cursor_us)/cursors : 0);
    if (g->pixels && count && save_frame(g, (size_t)g->width * g->height * 4))
        fprintf(stderr, "REIMS_FINAL_SNAPSHOT_ERROR\n");
    lab_frames_close(g->live_frames);
    if (g->target) g->rvg_frame_target_release(g->target);
    if (g->display) g->rvg_display_release(g->display);
    if (g->device) { g->rvg_device_stop(g->device); g->rvg_device_release(g->device); }
    if (g->iosurface) g->rvg_iosurface_device_release(g->iosurface);
    /* The header permits queued display callbacks after release. Keep their context and
     * the board RAM alive until all three callback tables have ended. */
    for (unsigned i = 0; i < 500 && atomic_load(&g->callbacks_live); ++i) usleep(10000);
    if (atomic_load(&g->callbacks_live)) { fprintf(stderr, "REIMS_CALLBACK_TEARDOWN_DEADLINE\n"); abort(); }
    free(g->pixels); free(g->previous_pixels); free(g->surface_pixels);
    free(g->cursor_rgba); pthread_mutex_destroy(&g->cursor_lock);
    /* The runtime owns process-global caches: never dlclose its library before process exit. */
    free(g);
}
HvAndroidReims *hv_android_reims_create(HvVmapple *board) {
    const char *library = getenv("HENGE_REIMS_LIBRARY"), *frame_path = getenv("HENGE_REIMS_FRAME");
    unsigned long required = (1UL << 8) | (1UL << 15) | (1UL << 25);
    if (!library || library[0] != '/' || !frame_path || board->ram_fd < 0 ||
        (getauxval(AT_HWCAP) & required) != required) return NULL;
    if (((uintptr_t)board->ram | board->ram_size) & 0x3fff) {
        fprintf(stderr, "REIMS_GUEST_RAM_ALIGNMENT_REQUIRED page_bytes=16384\n"); return NULL;
    }
    HvAndroidReims *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    if (pthread_mutex_init(&g->cursor_lock, NULL)) { free(g); return NULL; }
    g->board = board;
    if (snprintf(g->path, sizeof g->path, "%s", frame_path) >= sizeof g->path) goto failed;
    char live_path[4120];
    if (snprintf(live_path, sizeof live_path, "%s.live", frame_path) >= sizeof live_path) goto failed;
    g->live_frames = lab_frames_create(live_path, microseconds(CLOCK_REALTIME) / 1000);
    if (!g->live_frames) { perror("REIMS_TRANSPORT_CREATE_ERROR"); goto failed; }
    g->library = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!g->library) { fprintf(stderr, "REIMS_LOAD_ERROR %s\n", dlerror()); goto failed; }
#define LOAD(name) g->name = (__typeof__(&name))dlsym(g->library, #name); if (!g->name) goto failed;
    API_FUNCTIONS(LOAD)
#undef LOAD
    const char *debug = getenv("HENGE_REIMS_DEBUG");
    g->rvg_log_set_handler(log_line, NULL, debug && !strcmp(debug, "1") ? RVG_LOG_DEBUG : RVG_LOG_INFO);
    RvgMemoryRange range = {.physical_address = VMAPPLE_MEM, .physical_length = board->ram_size, .host_address = board->ram};
    RvgMemoryMap map = {.ranges = &range, .range_count = 1};
    RvgMemoryRangeFile file = {.fd = board->ram_fd};
    RvgIOSurfaceCallbacks ic = {.struct_size = sizeof ic, .ctx = g, .destroy = callbacks_end, .raise_interrupt = ios_irq};
    RvgIOSurfaceDeviceConfig isc = {.struct_size = sizeof isc, .memory_map = &map, .callbacks = &ic};
    atomic_fetch_add(&g->callbacks_live, 1);
    RvgStatus status = g->rvg_iosurface_device_create(&isc, &g->iosurface);
    if (status != RVG_OK) { fprintf(stderr, "REIMS_IOSURFACE_CREATE_ERROR %u\n", status); goto failed; }
    RvgHostCallbacks hc = {.struct_size = sizeof hc, .ctx = g, .destroy = callbacks_end, .raise_interrupt = gfx_irq};
    RvgDeviceConfig dc = {.struct_size = sizeof dc, .display_port_count = 1, .use_iosurface_mapper = true,
        .process_isolation = true, .process_isolation_follows_guest = true,
        .memory_map = &map, .memory_files = &file, .callbacks = &hc};
    atomic_fetch_add(&g->callbacks_live, 1);
    status = g->rvg_device_create(&dc, &g->device);
    if (status != RVG_OK) { fprintf(stderr, "REIMS_DEVICE_CREATE_ERROR %u\n", status); goto failed; }
    RvgDisplayCallbacks cb = {.struct_size = sizeof cb, .ctx = g, .destroy = callbacks_end,
        .mode_change = mode_change, .new_frame = new_frame,
        .cursor_glyph = cursor_glyph, .cursor_show = cursor_show, .cursor_move = cursor_move};
    RvgDisplayConfig display = {.struct_size = sizeof display, .serial_number = 1, .port = 0,
        .width_millimeters = 236.5, .height_millimeters = 177.4, .callbacks = &cb};
    atomic_fetch_add(&g->callbacks_live, 1);
    status = g->rvg_display_create(g->device, &display, &g->display);
    if (status != RVG_OK) { fprintf(stderr, "REIMS_DISPLAY_CREATE_ERROR %u\n", status); goto failed; }
    RvgDisplayMode modes[] = {
        {.width = 1024, .height = 768, .refresh_rate_hz = 60},
        {.width = 1280, .height = 800, .refresh_rate_hz = 60},
        {.width = 1600, .height = 1000, .refresh_rate_hz = 60}};
    g->rvg_display_set_modes(g->display, modes, sizeof modes / sizeof modes[0]);
    if (pthread_create(&g->presenter, NULL, presenter, g)) goto failed;
    g->presenter_started = 1;
    hv_machine_add_mmio(board->m, VMAPPLE_GFX, 0x10000, g, gfx_read, gfx_write, "reims-gpu");
    hv_machine_add_mmio(board->m, VMAPPLE_IOSFC, 0x10000, g, ios_read, ios_write, "reims-iosurface");
    printf("REIMS_BOARD_READY shared_ram_fd=%d ram_bytes=%" PRIu64 " ram_host=%p guest_page_aligned=%d preferred_mode=1024x768 surface_period_us=33333 cursor_period_us=16667 transport=shared-frames debug=%d\n",
        board->ram_fd, board->ram_size, board->ram, ((uintptr_t)board->ram & 0x3fff) == 0, debug && !strcmp(debug, "1"));
    return g;
failed:
    hv_android_reims_destroy(g);
    return NULL;
}
#else
int hv_android_reims_pointer(HvAndroidReims *g, unsigned *w, unsigned *h, int *x, int *y) { (void)g; (void)w; (void)h; (void)x; (void)y; return 0; }
HvAndroidReims *hv_android_reims_create(struct HvVmapple *board) {
    (void)board; fprintf(stderr, "REIMS_NOT_BUNDLED rebuild with --reims\n"); return NULL;
}
void hv_android_reims_poll(HvAndroidReims *gpu) { (void)gpu; }
void hv_android_reims_report(HvAndroidReims *gpu) { (void)gpu; }
void hv_android_reims_destroy(HvAndroidReims *gpu) { (void)gpu; }
#endif
