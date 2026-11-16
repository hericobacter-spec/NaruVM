/* Isolated public C API probe. No guest disk is opened or written. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include "reims_vgpu.h"

static void log_line(void *ctx, RvgLogLevel level, const char *target, size_t tn,
                     const char *message, size_t mn) {
    (void)ctx;
    fprintf(stderr, "REIMS_LOG level=%u %.*s: %.*s\n", level, (int)tn, target, (int)mn, message);
}
static void interrupt(void *ctx, uint32_t vector) {
    (void)ctx;
    fprintf(stderr, "REIMS_IRQ vector=%u\n", vector);
}
static atomic_bool got_frame;
static void new_frame(void *ctx) { (void)ctx; atomic_store(&got_frame, true); }
static uint32_t page_entry(uint32_t address) { return ((address >> 14) << 2) | 1; }
static void put32(uint8_t *bytes, unsigned at, uint32_t value) { memcpy(bytes + at, &value, 4); }
#define LOAD(name) __typeof__(&name) api_##name = (__typeof__(&name))dlsym(library, #name); \
    if (!api_##name) { fprintf(stderr, "missing %s: %s\n", #name, dlerror()); return 2; }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc < 2) { fprintf(stderr, "usage: reims_probe <library> [--efi|--readback-stress] [--surface] [--loader=path]\n"); return 2; }
    int efi = 0, surface = 0, readback_stress = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--efi")) efi = 1;
        else if (!strcmp(argv[i], "--readback-stress")) { efi = 1; readback_stress = 1; }
        else if (!strcmp(argv[i], "--surface")) surface = 1;
        else if (!strncmp(argv[i], "--loader=", 9) && argv[i][9]) {
            if (setenv("REIMS_VGPU_VULKAN_LOADER", argv[i] + 9, 1)) return 2;
        } else { fprintf(stderr, "invalid probe argument\n"); return 2; }
    }
    char cache[4096];
    if (!getcwd(cache, sizeof(cache) - 32)) { perror("getcwd"); return 2; }
    size_t end = __builtin_strlen(cache);
    __builtin_memcpy(cache + end, "/reims-shader-cache", sizeof("/reims-shader-cache"));
    if (mkdir(cache, 0700) && errno != EEXIST) { perror("mkdir cache"); return 2; }
    if (setenv("REIMS_VGPU_SHADER_CACHE_DIR", cache, 1)) { perror("setenv cache"); return 2; }
    unsigned long caps = getauxval(AT_HWCAP);
    printf("REIMS_PROBE guest=none disk=none hwcap=0x%lx\n", caps);
    /* This build targets LSE and RCpc and upstream's unaligned accesses require LSE2. */
    if ((caps & ((1UL << 8) | (1UL << 15) | (1UL << 25))) !=
                ((1UL << 8) | (1UL << 15) | (1UL << 25))) {
        fprintf(stderr, "REIMS_RESULT failures=1 reason=CPU-requires-atomics-lrcpc-uscat\n"); return 1;
    }
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    LOAD(rvg_log_set_handler); LOAD(rvg_device_create); LOAD(rvg_device_mmio_read);
    LOAD(rvg_device_reset); LOAD(rvg_device_stop); LOAD(rvg_device_release);
    LOAD(rvg_status_message);
    LOAD(rvg_device_mmio_write); LOAD(rvg_display_create); LOAD(rvg_display_set_modes);
    LOAD(rvg_display_release); LOAD(rvg_frame_target_create); LOAD(rvg_frame_target_release);
    LOAD(rvg_display_encode_frame); LOAD(rvg_frame_commit); LOAD(rvg_frame_wait);
    LOAD(rvg_frame_release); LOAD(rvg_frame_target_read_pixels);
    LOAD(rvg_iosurface_device_create); LOAD(rvg_iosurface_device_release);
    LOAD(rvg_iosurface_device_mmio_read); LOAD(rvg_iosurface_device_mmio_write);
    api_rvg_log_set_handler(log_line, NULL, RVG_LOG_INFO);
    const size_t length = 16 * 1024 * 1024;
    int fd = (int)syscall(__NR_memfd_create, "henge-reims-probe", 1U | 2U);
    if (fd < 0 || ftruncate(fd, length)) { perror("memfd"); return 2; }
    void *ram = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ram == MAP_FAILED) { perror("mmap"); return 2; }
    RvgMemoryRange range = { .physical_address = 0x70000000, .physical_length = length, .host_address = ram };
    RvgMemoryMap map = { .ranges = &range, .range_count = 1 };
    RvgMemoryRangeFile file = { .fd = fd, .flags = 0, .offset = 0 };
    RvgHostCallbacks callbacks = { .struct_size = sizeof(callbacks), .raise_interrupt = interrupt };
    RvgDeviceConfig config = { .struct_size = sizeof(config), .display_port_count = 1,
        .use_iosurface_mapper = true, .memory_map = &map, .memory_files = &file, .callbacks = &callbacks };
    RvgDevice *device = NULL;
    RvgStatus result = api_rvg_device_create(&config, &device);
    printf("REIMS_DEVICE_CREATE status=%u message=%s\n", result, api_rvg_status_message(result));
    int failures = result != RVG_OK;
    if (!failures) {
        uint32_t ports = api_rvg_device_mmio_read(device, 0x122c);
        printf("REIMS_MMIO display_ports=%u\n", ports);
        failures += ports != 1;
        if (surface) {
            /* Authored first-form descriptor, using the published repr(C) layout in
             * reims-vgpu-core/src/iosurface/descriptor.rs. No Apple binary is used.
             * A 16 KiB allocation must cover all 4096 pixels even on a 4 KiB host. */
            uint8_t *bytes = ram;
            memset(bytes + 0x4000, 0, 0x14000);
            put32(bytes, 0x8000, page_entry(0x7000c000));
            uint8_t *descriptor = bytes + 0xc000 + 0x200; /* mapping ID 1 */
            put32(descriptor, 0, page_entry(0x70010000));
            put32(descriptor, 4, 0x42475241); /* BGRA */
            put32(descriptor, 0x10, 0x4000);
            put32(descriptor, 0x14, (64 << 8) | 1);
            put32(descriptor, 0x18, (64 << 8) | 1);
            put32(descriptor, 0x1c, 256);
            descriptor[0x20] = 4;
            put32(bytes, 0x10000, page_entry(0x70014000));
            uint32_t *pixels = (uint32_t *)(bytes + 0x14000);
            for (unsigned i = 0; i < 4096; ++i) pixels[i] = 0x12340000 | i;
            uint32_t *ring = (uint32_t *)(bytes + 0x4000);
            ring[0] = 1; ring[1] = 1; ring[2] = 0xffffffff;
            RvgIOSurfaceCallbacks ic = {.struct_size = sizeof ic, .raise_interrupt = interrupt};
            RvgIOSurfaceDeviceConfig isc = {.struct_size = sizeof isc, .memory_map = &map, .callbacks = &ic};
            RvgIOSurfaceDevice *is = NULL;
            RvgStatus status = api_rvg_iosurface_device_create(&isc, &is);
            int failed = status != RVG_OK;
            uint32_t map_status = 0xffffffff, unmap_status = 0xffffffff;
            if (!failed) {
                api_rvg_iosurface_device_mmio_write(is, 0x1030, 0);
                api_rvg_iosurface_device_mmio_write(is, 0x1000, 0x70004000);
                api_rvg_iosurface_device_mmio_write(is, 0x1010, 0x70008000);
                api_rvg_iosurface_device_mmio_write(is, 0x1008, 1024);
                api_rvg_iosurface_device_mmio_write(is, 0x1018, 1);
                unsigned count = 0;
                while (api_rvg_iosurface_device_mmio_read(is, 0x1020) != 1 && count++ < 200) usleep(10000);
                map_status = __atomic_load_n(ring + 2, __ATOMIC_ACQUIRE);
                uint32_t mapped = __atomic_load_n((uint32_t *)descriptor, __ATOMIC_ACQUIRE);
                failed = count >= 200 || map_status != 0 || !(mapped & 2);
                if (!failed) {
                    ring[4] = 2; ring[5] = 1; ring[6] = 0xffffffff;
                    api_rvg_iosurface_device_mmio_write(is, 0x1018, 2);
                    count = 0;
                    while (api_rvg_iosurface_device_mmio_read(is, 0x1020) != 2 && count++ < 200) usleep(10000);
                    unmap_status = __atomic_load_n(ring + 6, __ATOMIC_ACQUIRE);
                    mapped = __atomic_load_n((uint32_t *)descriptor, __ATOMIC_ACQUIRE);
                    failed = count >= 200 || unmap_status != 0 || (mapped & 2);
                }
                for (unsigned i = 0; i < 4096 && !failed; ++i) failed = pixels[i] != (0x12340000 | i);
                api_rvg_iosurface_device_release(is);
            }
            printf("REIMS_SURFACE_RESULT failures=%d map_status=0x%x unmap_status=0x%x pixels=4096 guest_page_bytes=16384 host_page_bytes=%ld source=authored-guest-ram\n",
                failed, map_status, unmap_status, sysconf(_SC_PAGESIZE));
            failures += failed;
        }
        if (efi) {
            printf("REIMS_EFI_FIXTURE source=authored-guest-ram width=64 height=64 macOS=none\n");
            uint32_t *pixels = ram;
            for (unsigned i = 0; i < 4096; ++i) pixels[i] = 0xff00ffff;
            RvgDisplayCallbacks dc = { .struct_size = sizeof(dc), .new_frame = new_frame };
            RvgDisplayConfig display_config = { .struct_size = sizeof(display_config), .serial_number = 1,
                .port = 0, .width_millimeters = 64, .height_millimeters = 64, .callbacks = &dc };
            RvgDisplay *display = NULL;
            RvgStatus step = api_rvg_display_create(device, &display_config, &display);
            RvgFrameTarget *target = NULL;
            RvgFrame *frame = NULL;
            if (step == RVG_OK) {
                RvgDisplayMode mode = { .width = 64, .height = 64, .refresh_rate_hz = 60 };
                api_rvg_display_set_modes(display, &mode, 1);
                /* Enabling constructs the EFI receiver; subsequent writes reach that receiver. */
                api_rvg_device_mmio_write(device, 0x1200, 1);
                api_rvg_device_mmio_write(device, 0x1210, 0x70000000 >> 14);
                api_rvg_device_mmio_write(device, 0x1214, 0x4000);
                api_rvg_device_mmio_write(device, 0x1218, 32);
                api_rvg_device_mmio_write(device, 0x121c, 64 | (64 << 16));
                api_rvg_device_mmio_write(device, 0x1228, 256);
                for (unsigned i = 0; i < 200 && !atomic_load(&got_frame); ++i) usleep(10000);
                if (!atomic_load(&got_frame)) { fprintf(stderr, "EFI callback deadline expired\n"); step = RVG_ERR_GPU; }
            }
            RvgFrameTargetConfig tc = { .struct_size = sizeof(tc), .width = 64, .height = 64,
                .format = RVG_PIXEL_FORMAT_BGRA8_UNORM };
            if (step == RVG_OK) step = api_rvg_frame_target_create(device, &tc, &target);
            if (step == RVG_OK) step = api_rvg_display_encode_frame(display, target, NULL, &frame);
            if (step == RVG_OK) step = api_rvg_frame_commit(frame);
            if (step == RVG_OK) step = api_rvg_frame_wait(frame);
            uint32_t readback[4096];
            if (step == RVG_OK) step = api_rvg_frame_target_read_pixels(target, readback, 256, sizeof(readback));
            if (step == RVG_OK) for (unsigned i = 0; i < 4096; ++i) {
                if (readback[i] != 0xff00ffff) {
                    fprintf(stderr, "EFI pixel %u got=0x%08x expected=0xff00ffff\n", i, readback[i]);
                    step = RVG_ERR_GPU; break;
                }
            }
            if (readback_stress && step == RVG_OK) {
                if (frame) { api_rvg_frame_release(frame); frame = NULL; }
                for (unsigned round = 0; round < 30 && step == RVG_OK; ++round) {
                    unsigned side = 32 + (round / 10) * 32, stride = side * 4 + 16;
                    size_t extent = (size_t)stride * side;
                    unsigned char padded[96 * (96 * 4 + 16)];
                    if (round % 10 == 0) {
                        api_rvg_frame_target_release(target); target = NULL;
                        tc.width = tc.height = side;
                        step = api_rvg_frame_target_create(device, &tc, &target);
                        memset(padded, 0xa5, extent);
                        if (step == RVG_OK) step = api_rvg_frame_target_read_pixels(target, padded, stride, extent);
                        for (unsigned y = 0; y < side && step == RVG_OK; ++y) {
                            for (unsigned x = 0; x < stride; ++x) {
                                unsigned expected = x < side * 4 ? 0 : 0xa5;
                                if (padded[y * stride + x] != expected) { step = RVG_ERR_GPU; break; }
                            }
                        }
                    }
                    uint32_t color = round % 2 ? 0xff40ff20 : 0xff00ffff;
                    uint32_t *guest_pixels = ram;
                    for (unsigned i = 0; i < 4096; ++i) guest_pixels[i] = color;
                    /* No trace range in this authored host: EFI presents from its 100ms timer. */
                    usleep(120000);
                    if (step == RVG_OK) step = api_rvg_display_encode_frame(display, target, NULL, &frame);
                    if (step == RVG_OK) step = api_rvg_frame_commit(frame);
                    if (step == RVG_OK) step = api_rvg_frame_wait(frame);
                    memset(padded, 0xa5, extent);
                    if (step == RVG_OK) step = api_rvg_frame_target_read_pixels(target, padded, stride, extent);
                    for (unsigned y = 0; y < side && step == RVG_OK; ++y) {
                        for (unsigned x = 0; x < side; ++x) {
                            uint32_t pixel; memcpy(&pixel, padded + y * stride + x * 4, 4);
                            if (pixel != color) { step = RVG_ERR_GPU; break; }
                        }
                        for (unsigned x = side * 4; x < stride; ++x)
                            if (padded[y * stride + x] != 0xa5) step = RVG_ERR_GPU;
                    }
                    if (frame) { api_rvg_frame_release(frame); frame = NULL; }
                    if (step != RVG_OK) fprintf(stderr, "READBACK_C_API_ERROR round=%u side=%u status=%u\n", round, side, step);
                }
                printf("READBACK_C_API_RESULT failures=%d rounds=30 sizes=32,64,96 padding=checked initial_zero=checked\n", step != RVG_OK);
            }
            if (frame) api_rvg_frame_release(frame);
            if (target) api_rvg_frame_target_release(target);
            if (display) api_rvg_display_release(display);
            failures += step != RVG_OK;
            printf("REIMS_FRAME_RESULT failures=%d status=%u pixels=4096 source=authored-guest-ram\n", step != RVG_OK, step);
        }
        api_rvg_device_reset(device);
        api_rvg_device_stop(device);
        api_rvg_device_release(device);
    }
    munmap(ram, length); close(fd);
    /* The runtime holds process-global GPU caches; leave its library mapped until exit. */
    printf("REIMS_RESULT failures=%d guest=none disk=none\n", failures);
    return failures ? 1 : 0;
}
