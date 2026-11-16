/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own-memory API acceptance probe, not a GPU execution or guest acceleration test.
 * UAPI: Arm r44p0 mali_kbase_ioctl.h / mali_base_kernel.h; CSF version ioctl 52.
 * Never imports another process, submits GPU commands, or touches a guest disk.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/sync_file.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

struct version_check { uint16_t major, minor; };
struct set_flags { uint32_t flags; };
struct user_buffer { uint64_t ptr, length; };
union mem_import {
    struct { uint64_t flags, phandle; uint32_t type, padding; } in;
    struct { uint64_t flags, gpu_va, pages; } out;
};
_Static_assert(sizeof(union mem_import) == 24, "Arm r44 import ABI");
#define VERSION_CHECK _IOWR(0x80, 52, struct version_check)
#define SET_FLAGS _IOW(0x80, 1, struct set_flags)
#define MEM_IMPORT _IOWR(0x80, 22, union mem_import)
#define CACHED_CPU (1ull << 12)
#define USER_BUFFER 3
#define SIZE (64u * 1024u)
#define CHUNK (16u * 1024u)
struct kcpu_queue { uint8_t id, padding[7]; };
struct kcpu_enqueue { uint64_t commands; uint32_t count; uint8_t id, padding[3]; };
struct kcpu_command { uint8_t type, padding[7]; uint64_t info[2]; };
struct base_fence { int fd, stream_fd; };
#define KCPU_CREATE _IOR(0x80, 45, struct kcpu_queue)
#define KCPU_DELETE _IOW(0x80, 46, struct kcpu_queue)
#define KCPU_ENQUEUE _IOW(0x80, 47, struct kcpu_enqueue)

static int enqueue(int fd, unsigned id, struct kcpu_command *cmd) {
    struct kcpu_enqueue req = {.commands = (uintptr_t)cmd, .count = 1, .id = id};
    return ioctl(fd, KCPU_ENQUEUE, &req);
}

static int wait_queue(int fd, unsigned id) {
    struct base_fence fence = {-1, -1};
    struct kcpu_command signal = {.type = 0, .info = {(uintptr_t)&fence}};
    if (enqueue(fd, id, &signal)) return -1;
    if (fence.fd < 0) { errno = EIO; return -1; }
    struct pollfd pollfd = {.fd = fence.fd, .events = POLLIN};
    int ready = poll(&pollfd, 1, 5000);
    struct sync_file_info info = {0};
    int status = ready > 0 ? ioctl(fence.fd, SYNC_IOC_FILE_INFO, &info) : -1;
    int saved = ready == 0 ? ETIMEDOUT : errno;
    close(fence.fd);
    if (status || info.status <= 0) { errno = status ? saved : EIO; return -1; }
    return 0;
}

static int check_import(const char *name, void *memory, const uint32_t *expected, int map_import, int system_coherent) {
    int fd = open("/dev/mali0", O_RDWR | O_CLOEXEC);
    void *tracking = MAP_FAILED;
    int result = 1;
    void *gpu_anchor = MAP_FAILED;
    struct kcpu_queue queue = {0};
    int queue_created = 0, map_enqueued = 0;
    if (fd < 0) { printf("MALI_USERPTR_OPEN shape=%s errno=%d\n", name, errno); return 1; }
    struct version_check version = {1, 21};
    if (ioctl(fd, VERSION_CHECK, &version)) {
        printf("MALI_USERPTR_VERSION shape=%s errno=%d\n", name, errno); goto out;
    }
    printf("MALI_USERPTR_VERSION shape=%s api=%u.%u\n", name, version.major, version.minor);
    struct set_flags flags = {0};
    if (ioctl(fd, SET_FLAGS, &flags)) {
        printf("MALI_USERPTR_SET_FLAGS shape=%s errno=%d\n", name, errno); goto out;
    }
    tracking = mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, 3ull << 12);
    if (tracking == MAP_FAILED) {
        printf("MALI_USERPTR_TRACKING shape=%s errno=%d\n", name, errno); goto out;
    }
    struct user_buffer own = {(uintptr_t)memory, SIZE};
    uint64_t import_flags = CACHED_CPU | 0xfull;
    if (system_coherent) import_flags |= (1ull << 10) | (1ull << 15);
    printf("MALI_USERPTR_REQUEST shape=%s flags=0x%" PRIx64 " required_system_coherent=%d\n", name, import_flags, system_coherent);
    union mem_import req = {.in = {.flags = import_flags,
        .phandle = (uintptr_t)&own, .type = USER_BUFFER}};
    if (ioctl(fd, MEM_IMPORT, &req)) {
        printf("MALI_USERPTR_IMPORT shape=%s accepted=0 errno=%d\n", name, errno); goto out;
    }
    printf("MALI_USERPTR_IMPORT shape=%s accepted=1 pages=%" PRIu64 " flags=0x%" PRIx64 "\n",
        name, req.out.pages, req.out.flags);
    if (req.out.pages != SIZE / (uint64_t)sysconf(_SC_PAGESIZE)) {
        printf("MALI_USERPTR_SIZE_MISMATCH shape=%s\n", name); goto out;
    }
    if (map_import) {
        gpu_anchor = mmap(NULL, SIZE, PROT_NONE, MAP_SHARED, fd, req.out.gpu_va);
        if (gpu_anchor == MAP_FAILED) {
            printf("MALI_USERPTR_GPU_VA shape=%s accepted=0 errno=%d\n", name, errno); goto out;
        }
        if (ioctl(fd, KCPU_CREATE, &queue)) {
            printf("MALI_USERPTR_KCPU_CREATE shape=%s errno=%d\n", name, errno); goto out;
        }
        queue_created = 1;
        struct kcpu_command map = {.type = 6, .info = {(uintptr_t)gpu_anchor}};
        if (enqueue(fd, queue.id, &map)) {
            printf("MALI_USERPTR_MAP_IMPORT shape=%s accepted=0 errno=%d\n", name, errno); goto out;
        }
        map_enqueued = 1;
        if (wait_queue(fd, queue.id)) {
            printf("MALI_USERPTR_MAP_FENCE shape=%s errno=%d\n", name, errno); goto out;
        }
        printf("MALI_USERPTR_MAP_IMPORT shape=%s pin_map_fence=pass\n", name);
    }
    const uint32_t *words = memory;
    for (size_t i = 0; i < SIZE / sizeof(*words); i++) {
        if (words[i] != expected[i]) {
            printf("MALI_USERPTR_CPU_PATTERN_MISMATCH shape=%s word=%zu\n", name, i); goto out;
        }
    }
    result = 0;
out:
    if (map_enqueued) {
        struct kcpu_command unmap = {.type = 7, .info = {(uintptr_t)gpu_anchor}};
        if (enqueue(fd, queue.id, &unmap) || wait_queue(fd, queue.id)) {
            printf("MALI_USERPTR_UNMAP_IMPORT shape=%s errno=%d\n", name, errno); result = 1;
        } else printf("MALI_USERPTR_UNMAP_IMPORT shape=%s unpin_fence=pass\n", name);
    }
    if (queue_created && ioctl(fd, KCPU_DELETE, &queue)) {
        printf("MALI_USERPTR_KCPU_DELETE shape=%s errno=%d\n", name, errno); result = 1;
    }
    if (gpu_anchor != MAP_FAILED) munmap(gpu_anchor, SIZE);
    /* Closing this private context releases all imports, including pending cookies.
     * CPU buffers stay mapped until after context teardown. No GPU jobs exist. */
    if (tracking != MAP_FAILED) munmap(tracking, 4096);
    close(fd);
    return result;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("MALI_USERPTR_SCOPE owned_64KiB_only no_gpu_commands no_guest_access\n");
    int map_import = argc == 2 && !strcmp(argv[1], "--map-import");
    int system_coherent = argc == 2 && !strcmp(argv[1], "--require-system-coherent");
    if (system_coherent) map_import = 1;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || CHUNK % (unsigned long)page) return 2;
    int backing = syscall(SYS_memfd_create, "henge-own-userptr", 1u);
    if (backing < 0 || ftruncate(backing, SIZE)) { if (backing >= 0) close(backing); return 2; }
    uint32_t *canonical = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, backing, 0);
    void *scatter = mmap(NULL, SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (canonical == MAP_FAILED || scatter == MAP_FAILED) {
        if (canonical != MAP_FAILED) munmap(canonical, SIZE);
        if (scatter != MAP_FAILED) munmap(scatter, SIZE);
        close(backing); return 2;
    }
    const unsigned order[] = {0, 3, 1, 2};
    for (unsigned i = 0; i < 4; i++) {
        void *at = (char *)scatter + i * CHUNK;
        /* MAP_FIXED is confined to our own PROT_NONE reservation. */
        if (mmap(at, CHUNK, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
                 backing, order[i] * CHUNK) != at) {
            munmap(scatter, SIZE); munmap(canonical, SIZE); close(backing); return 2;
        }
    }
    uint32_t contiguous_expected[SIZE / 4], scatter_expected[SIZE / 4];
    for (size_t i = 0; i < SIZE / 4; i++) canonical[i] = contiguous_expected[i] = 3 * i + 19;
    for (size_t i = 0; i < SIZE / 4; i++) {
        size_t block = i / (CHUNK / 4), offset = i % (CHUNK / 4);
        scatter_expected[i] = contiguous_expected[order[block] * (CHUNK / 4) + offset];
    }
    int failed = check_import("contiguous_memfd", canonical, contiguous_expected, map_import, system_coherent);
    failed += check_import("scatter_memfd_0_3_1_2", scatter, scatter_expected, map_import, system_coherent);
    munmap(scatter, SIZE); munmap(canonical, SIZE); close(backing);
    printf("MALI_USERPTR_RESULT accepted_shapes=%d/2 gpu_execution=untested macos_acceleration=disabled\n", 2 - failed);
    return failed ? 1 : 0;
}
