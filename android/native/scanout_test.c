/* Authored pixels exercise the actual adapter's capture path, without a VM/GPU. */
#ifdef HENGE_SCANOUT_REFERENCE
#include HENGE_SCANOUT_REFERENCE
#else
#include "reims_gpu.c"
#endif
#include <sys/stat.h>
static unsigned encoded, committed, waited, readbacks, fixture_width, fixture_height;
static uint32_t fixture_color = 0xff123456;
static RvgDisplayCoord position = {.x = 1, .y = 1};
static int failures, checks;
static void check(const char *name, int ok) { checks++; failures += !ok; printf("%s %s\n", ok ? "PASS" : "FAIL", name); }
static RvgStatus target_create(RvgDevice *device, const RvgFrameTargetConfig *config, RvgFrameTarget **out) {
    fixture_width = config->width; fixture_height = config->height; *out = (RvgFrameTarget *)(uintptr_t)1; return RVG_OK;
}
static void target_release(RvgFrameTarget *target) { }
static RvgStatus encode(RvgDisplay *display, RvgFrameTarget *target, const RvgRegion *region, RvgFrame **out) {
    encoded++; *out = (RvgFrame *)(uintptr_t)1; return RVG_OK;
}
static RvgStatus commit(RvgFrame *frame) { committed++; return RVG_OK; }
static RvgStatus wait_frame(RvgFrame *frame) { waited++; return RVG_OK; }
static void release_frame(RvgFrame *frame) { }
static RvgStatus read_pixels(const RvgFrameTarget *target, void *pixels, size_t stride, size_t bytes) {
    if (stride != fixture_width * 4 || bytes != (size_t)fixture_width * fixture_height * 4) return RVG_ERR_INVALID_ARGUMENT;
    uint32_t *out = pixels; for (size_t i = 0; i < bytes / 4; i++) out[i] = fixture_color;
    readbacks++; return RVG_OK;
}
static RvgDisplayCoord cursor_position(const RvgDisplay *display) { return position; }
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    char folder[4096], frames[4120];
    if (snprintf(folder, sizeof folder, "%s/henge-scanout-XXXXXX", argv[1]) >= sizeof folder || !mkdtemp(folder)) return 1;
    HvAndroidReims g = {0}; pthread_mutex_init(&g.cursor_lock, NULL);
    snprintf(g.path, sizeof g.path, "%s/frame.bmp", folder); snprintf(frames, sizeof frames, "%s/frames.bin", folder);
    g.live_frames = lab_frames_create(frames, 1);
    if (!g.live_frames) return 1;
    g.rvg_frame_target_create = target_create; g.rvg_frame_target_release = target_release;
    g.rvg_display_encode_frame = encode; g.rvg_frame_commit = commit; g.rvg_frame_wait = wait_frame;
    g.rvg_frame_release = release_frame; g.rvg_frame_target_read_pixels = read_pixels;
    g.rvg_display_cursor_position = cursor_position;
    mode_change(&g, 64, 32, 0);
    check("first cursor notification populates empty surface cache", capture(&g, DIRTY_CURSOR) == RVG_OK && encoded == 1 && readbacks == 1);
    const uint8_t red[] = {255, 0, 0, 255};
    RvgCursorGlyph glyph = {.struct_size = sizeof glyph, .width = 1, .height = 1, .rgba = red, .rgba_length = sizeof red};
    cursor_glyph(&g, &glyph); cursor_show(&g, 1);
    check("cursor overlay reuses surface without GPU submission", capture(&g, DIRTY_CURSOR) == RVG_OK && encoded == 1 && committed == 1 && waited == 1 && readbacks == 1 && g.pixels[65] == 0xffff0000);
    position = (RvgDisplayCoord){.x = 3, .y = 2}; cursor_move(&g);
    check("cursor movement restores old pixels without trails", capture(&g, DIRTY_CURSOR) == RVG_OK && g.pixels[65] == fixture_color && g.pixels[131] == 0xffff0000 && encoded == 1);
    cursor_show(&g, 0);
    int hidden = capture(&g, DIRTY_CURSOR) == RVG_OK;
    for (unsigned i = 0; i < 64 * 32; i++) if (g.pixels[i] != fixture_color) hidden = 0;
    check("hidden cursor restores entire cached surface", hidden && encoded == 1);
    fixture_color = 0xffabcdef; new_frame(&g);
    check("guest present refreshes cache through encode commit fence readback", capture(&g, DIRTY_SURFACE) == RVG_OK && g.pixels[0] == fixture_color && encoded == 2 && committed == 2 && waited == 2 && readbacks == 2);
    atomic_store(&g.dirty, 0); cursor_move(&g); new_frame(&g);
    unsigned dirty = atomic_exchange(&g.dirty, 0);
    check("coalesced cursor and surface preserve new present", dirty == (DIRTY_CURSOR | DIRTY_SURFACE) && capture(&g, dirty) == RVG_OK && encoded == 3);
    mode_change(&g, 80, 50, 0);
    check("resize invalidates cache even for cursor-only request", capture(&g, DIRTY_CURSOR) == RVG_OK && g.width == 80 && g.height == 50 && encoded == 4 && g.pixels[3999] == fixture_color);
    position = (RvgDisplayCoord){.x = UINT16_MAX, .y = UINT16_MAX}; cursor_show(&g, 1);
    check("out-of-range cursor leaves image unchanged", capture(&g, DIRTY_CURSOR) == RVG_OK && encoded == 4 && g.pixels[3999] == fixture_color);
    uint64_t unchanged = lab_frames_published(g.live_frames);
    check("unchanged composition does not publish", capture(&g, DIRTY_CURSOR) == RVG_OK && lab_frames_published(g.live_frames) == unchanged);
    mode_change(&g, 1024, 768, 0);
    check("desktop resize initializes both composition buffers", capture(&g, DIRTY_SURFACE) == RVG_OK && g.pixels[1024*768-1] == fixture_color);
    uint64_t begin = microseconds(CLOCK_MONOTONIC);
    int exact = 1;
    for (unsigned i=0;i<500;i++) {
        fixture_color = 0xff000000u | (i*7919);
        if (capture(&g, DIRTY_SURFACE) != RVG_OK || g.pixels[0] != fixture_color || g.pixels[1024*768-1] != fixture_color) exact=0;
        if (capture(&g, DIRTY_CURSOR) != RVG_OK || g.pixels[0] != fixture_color) exact=0;
    }
    printf("SCANOUT_COPY_BENCH captures=1000 width=1024 height=768 elapsed_us=%" PRIu64 "\n",microseconds(CLOCK_MONOTONIC)-begin);
    check("alternating changed and unchanged desktop frames preserve pixels",exact);
    mode_change(&g, 4097, 1, 0);
    unsigned before_oversize = encoded;
    check("oversize mode rejected before allocation", capture(&g, DIRTY_SURFACE) == RVG_ERR_INVALID_ARGUMENT && encoded == before_oversize);
    printf("SCANOUT_CACHE_FIXTURE_RESULT checks=%d failures=%d gpu_submissions=%u cursor_only=%" PRIuFAST64 "\n", checks, failures, encoded, atomic_load(&g.cursor_count));
    free(g.pixels); free(g.previous_pixels); free(g.surface_pixels); free(g.cursor_rgba);
    pthread_mutex_destroy(&g.cursor_lock); lab_frames_close(g.live_frames);
    unlink(g.path); unlink(frames); rmdir(folder);
    return failures ? 1 : 0;
}
