/* No renderer is implemented on Android. Keep this failure explicit. */
#include <stdio.h>
#include "pvg_lite.h"
HvPvgMetal *hv_pvg_metal_create(HvPvgLite *p) {
    (void)p;
    fprintf(stderr, "GRAPHICS_UNAVAILABLE: Android Metal renderer is not implemented; no desktop output.\n");
    return NULL;
}
void hv_pvg_metal_destroy(HvPvgMetal *m) { (void)m; }
void hv_pvg_metal_submit(HvPvgMetal *m, HvPvgLite *p, uint32_t c, uint32_t *cmd, uint32_t n) { (void)m; (void)p; (void)c; (void)cmd; (void)n; }
int hv_pvg_metal_snapshot(HvPvgMetal *m, uint8_t *b, uint32_t w, uint32_t h) { (void)m; (void)b; (void)w; (void)h; return -1; }
int hv_pvg_metal_frame(HvPvgMetal *m, const uint8_t **b, uint32_t *w, uint32_t *h) { (void)m; (void)b; (void)w; (void)h; return -1; }
void hv_pvg_metal_unmap(HvPvgMetal *m, uint32_t n) { (void)m; (void)n; }
void hv_pvg_metal_cursor_moved(HvPvgMetal *m) { (void)m; }
void hv_pvg_metal_set_layer(HvPvgMetal *m, void *l) { (void)m; (void)l; }
uint64_t hv_pvg_metal_gpu_frames(HvPvgMetal *m) { (void)m; return 0; }
void hv_pvg_metal_quiesce(HvPvgMetal *m) { (void)m; }
size_t hv_pvg_metal_display_state(HvPvgMetal *m, void *o, size_t n) { (void)m; (void)o; (void)n; return 0; }
void hv_pvg_metal_set_display_state(HvPvgMetal *m, const void *o, size_t n) { (void)m; (void)o; (void)n; }
