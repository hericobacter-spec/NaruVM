#ifndef HENGE_ANDROID_REIMS_GPU_H
#define HENGE_ANDROID_REIMS_GPU_H
struct HvVmapple;
typedef struct HvAndroidReims HvAndroidReims;
HvAndroidReims *hv_android_reims_create(struct HvVmapple *board);
int hv_android_reims_pointer(HvAndroidReims *gpu, unsigned *width, unsigned *height, int *x, int *y);
void hv_android_reims_poll(HvAndroidReims *gpu);
void hv_android_reims_report(HvAndroidReims *gpu);
void hv_android_reims_destroy(HvAndroidReims *gpu);
#endif
