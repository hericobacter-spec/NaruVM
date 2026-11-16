/* Isolated, exclusive-create disk clone. The source is always opened read-only.
 * Clone failure removes only the new files this invocation created. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "disk_clone_guard.h"

#define CHUNK (1u << 20)
static int io_all(int fd, void *buffer, size_t length, uint64_t at, int writing) {
    uint8_t *bytes = buffer;
    while (length) {
        ssize_t count = writing ? pwrite(fd, bytes, length, (off_t)at) : pread(fd, bytes, length, (off_t)at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        bytes += count; at += (uint64_t)count; length -= (size_t)count;
    }
    return 0;
}
static int same_source(const struct stat *a, const struct stat *b) {
    int same = a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
        a->st_mtime == b->st_mtime && a->st_ctime == b->st_ctime;
#if defined(__APPLE__)
    return same && a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec && a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
#else
    return same && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
#endif
}
static int verify_span(int source, int destination, uint64_t at, size_t length, uint8_t *a, uint8_t *b) {
    return io_all(source, a, length, at, 0) || io_all(destination, b, length, at, 0) || memcmp(a, b, length) ? -1 : 0;
}
static int clone_file(const char *source, const char *destination, uint64_t wanted) {
    int result = -1, out = -1, owns_out = 0;
    int in = open(source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return -1;
    struct stat before, after;
    if (fstat(in, &before) || !S_ISREG(before.st_mode) || (wanted && (uint64_t)before.st_size != wanted)) goto done;
    uint64_t size = (uint64_t)before.st_size;
    out = open(destination, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (out < 0) goto done;
    owns_out = 1;
    uint8_t *data = malloc(CHUNK), *readback = malloc(CHUNK), *zeros = calloc(1, CHUNK);
    if (!data || !readback || !zeros) { free(data); free(readback); free(zeros); goto done; }
    int reflink = 0;
#if defined(__linux__)
    reflink = ioctl(out, 0x40049409UL /* FICLONE */, in) == 0;
#endif
    if (reflink) {
        /* The kernel clones the extents. Check boundary and evenly spaced sample bytes too. */
        result = 0;
        for (unsigned i = 0; i <= 16 && !result; ++i) {
            size_t length = size < CHUNK ? (size_t)size : CHUNK;
            uint64_t at = size > length ? ((size - length) / 16) * i : 0;
            result = verify_span(in, out, at, length, data, readback);
        }
        printf("CLONE_FILE method=reflink bytes=%" PRIu64 " source=%s\n", size, source);
    } else {
        struct statvfs space;
        uint64_t allocated = (uint64_t)before.st_blocks * 512;
        if (fstatvfs(out, &space) || (uint64_t)space.f_bavail * space.f_frsize < allocated + (2ull << 30)) {
            fprintf(stderr, "CLONE_INSUFFICIENT_SPACE allocated=%" PRIu64 " reserve=2147483648\n", allocated);
            result = -1;
        } else if (ftruncate(out, (off_t)size)) result = -1;
        else {
            result = 0;
            uint64_t at = 0, copied = 0, next_report = 1ull << 30;
            int seeks = 1;
            while (at < size && !result) {
                uint64_t end = size;
                if (seeks) {
                    off_t start = lseek(in, (off_t)at, SEEK_DATA);
                    if (start < 0 && errno == ENXIO) break;
                    if (start < 0 && (errno == EINVAL || errno == ENOTSUP)) seeks = 0;
                    else if (start < 0) { result = -1; break; }
                    else {
                        off_t hole = lseek(in, start, SEEK_HOLE);
                        if (hole <= start) { result = -1; break; }
                        at = (uint64_t)start; end = (uint64_t)hole < size ? (uint64_t)hole : size;
                    }
                }
                while (at < end && !result) {
                    size_t length = end - at < CHUNK ? (size_t)(end - at) : CHUNK;
                    if (io_all(in, data, length, at, 0)) { result = -1; break; }
                    if (memcmp(data, zeros, length)) {
                        if (fstatvfs(out, &space) || (uint64_t)space.f_bavail * space.f_frsize < (2ull << 30) + length) {
                            fprintf(stderr, "CLONE_RESERVE_REACHED\n"); result = -1; break;
                        }
                        if (io_all(out, data, length, at, 1) || io_all(out, readback, length, at, 0) || memcmp(data, readback, length)) {
                            result = -1; break;
                        }
                        copied += length;
                    }
                    at += length;
                    if (copied >= next_report) {
                        printf("CLONE_PROGRESS copied=%" PRIu64 " scanned=%" PRIu64 " total=%" PRIu64 "\n", copied, at, size);
                        next_report = copied + (1ull << 30);
                    }
                }
            }
            printf("CLONE_FILE method=sparse-copy bytes=%" PRIu64 " written=%" PRIu64 " source=%s\n", size, copied, source);
        }
    }
    free(data); free(readback); free(zeros);
    if (!result && (fsync(out) || fstat(in, &after) || !same_source(&before, &after))) result = -1;
done:
    if (out >= 0) close(out);
    close(in);
    if (result && owns_out) unlink(destination);
    return result;
}

static int path(char *out, size_t capacity, const char *dir, const char *name) {
    int length = snprintf(out, capacity, "%s/%s", dir, name);
    return length < 0 || (size_t)length >= capacity ? -1 : 0;
}
static int clone_disks(const char *source, const char *destination) {
    /* An existing directory is never treated as a completed clone and never overwritten. */
    if (mkdir(destination, 0700)) { perror("exclusive clone directory"); return 1; }
    const char *names[] = {"root.raw", "auxiliary.bin"};
    const uint64_t sizes[] = {64ull << 30, 33570816};
    char from[4096], to[4096];
    unsigned completed = 0;
    for (; completed < 2; ++completed) {
        if (path(from, sizeof from, source, names[completed]) || path(to, sizeof to, destination, names[completed]) ||
            clone_file(from, to, sizes[completed])) break;
    }
    int failed = completed != 2;
    if (!failed) {
        if (path(to, sizeof to, destination, "research-clone.txt")) failed = 1;
        else {
            int marker = open(to, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            if (marker < 0) failed = 1;
            else {
                char contents[8192];
                int n = snprintf(contents, sizeof contents, "HENGE_RESEARCH_CLONE_V1\nsource=%s\nroot=68719476736\nauxiliary=33570816\n", source);
                failed = n < 0 || (size_t)n >= sizeof contents || io_all(marker, contents, (size_t)n, 0, 1) || fsync(marker);
                close(marker);
            }
        }
    }
    if (!failed) {
        int parent = open(destination, O_RDONLY | O_DIRECTORY);
        if (parent < 0 || fsync(parent)) failed = 1;
        if (parent >= 0) close(parent);
    }
    if (failed) {
        for (unsigned i = 0; i < completed; ++i) if (!path(to, sizeof to, destination, names[i])) unlink(to);
        if (!path(to, sizeof to, destination, "research-clone.txt")) unlink(to);
        rmdir(destination);
    }
    printf("CLONE_RESULT failures=%d source_readonly=1 destination=%s\n", failed, destination);
    return failed;
}

static int pair_test(const char *folder) {
    char source[4096], destination[4096], bad[4096], file[4096];
    path(source, sizeof source, folder, "pair-source"); path(destination, sizeof destination, folder, "pair-clone");
    path(bad, sizeof bad, folder, "pair-failed");
    if (mkdir(source, 0700)) return 1;
    const char *names[] = {"root.raw", "auxiliary.bin"};
    const uint64_t sizes[] = {64ull << 30, 33570816};
    int failures = 0;
    for (unsigned i = 0; i < 2; ++i) {
        path(file, sizeof file, source, names[i]);
        int fd = open(file, O_CREAT | O_EXCL | O_RDWR, 0600);
        uint8_t byte = (uint8_t)(0x41 + i);
        if (fd < 0 || ftruncate(fd, sizes[i]) || io_all(fd, &byte, 1, 16384, 1)) failures++;
        if (fd >= 0) close(fd);
    }
    if (!failures && clone_disks(source, destination)) failures++;
    if (!failures) {
        for (unsigned i = 0; i < 2; ++i) {
            path(file, sizeof file, destination, names[i]);
            int fd = open(file, O_RDONLY); uint8_t byte = 0; struct stat st;
            if (fd < 0 || fstat(fd, &st) || (uint64_t)st.st_size != sizes[i] ||
                io_all(fd, &byte, 1, 16384, 0) || byte != 0x41 + i) failures++;
            if (fd >= 0) close(fd);
        }
        path(file, sizeof file, destination, "research-clone.txt");
        FILE *marker = fopen(file, "r"); char line[64];
        if (!marker || !fgets(line, sizeof line, marker) || strcmp(line, "HENGE_RESEARCH_CLONE_V1\n")) failures++;
        if (marker) fclose(marker);
        failures += clone_disks(source, destination) == 0;
    }
    if (!failures) {
        failures += hv_research_clone_guard(destination) != 0;
        failures += hv_research_clone_guard(source) == 0;
        path(file, sizeof file, source, "research-clone.txt");
        FILE *forged = fopen(file, "w");
        if (!forged) failures++;
        else {
            fprintf(forged, "HENGE_RESEARCH_CLONE_V1\nsource=%s\n", source);
            if (fclose(forged)) failures++;
            failures += hv_research_clone_guard(source) == 0;
            unlink(file);
        }
        char held[4096], cloned[4096];
        path(held, sizeof held, destination, "held-root");
        path(cloned, sizeof cloned, destination, "root.raw");
        path(file, sizeof file, source, "root.raw");
        int hardlink_tested = 0;
        if (rename(cloned, held)) failures++;
        else {
            if (link(file, cloned)) {
                if (errno != EACCES && errno != EPERM && errno != EOPNOTSUPP && errno != EXDEV) failures++;
                printf("CLONE_GUARD_HARDLINK_FIXTURE unavailable_errno=%d\n", errno);
            } else {
                hardlink_tested = 1;
                failures += hv_research_clone_guard(destination) == 0;
                unlink(cloned);
            }
        }
        if (rename(held, cloned)) failures++;
        path(held, sizeof held, destination, "held-aux");
        path(cloned, sizeof cloned, destination, "auxiliary.bin");
        path(file, sizeof file, source, "auxiliary.bin");
        if (rename(cloned, held) || symlink(file, cloned)) failures++;
        else {
            failures += hv_research_clone_guard(destination) == 0;
            unlink(cloned);
        }
        if (rename(held, cloned)) failures++;
        printf("CLONE_GUARD_TEST failures=%d fixture=private-temporary-files hardlink_tested=%d cases=valid,missing-marker,same-inode,symlink\n", failures, hardlink_tested);
    }
    path(file, sizeof file, source, "auxiliary.bin");
    if (truncate(file, 1) || clone_disks(source, bad) == 0 || access(bad, F_OK) == 0) failures++;
    const char *dirs[] = {source, destination, bad};
    for (unsigned d = 0; d < 3; ++d) {
        for (unsigned i = 0; i < 2; ++i) { path(file, sizeof file, dirs[d], names[i]); unlink(file); }
        path(file, sizeof file, dirs[d], "research-clone.txt"); unlink(file); rmdir(dirs[d]);
    }
    return failures;
}

static int selftest(const char *parent) {
    char folder[4096];
    int length = snprintf(folder, sizeof folder, "%s/clone-check-XXXXXX", parent);
    if (length < 0 || (size_t)length >= sizeof folder || !mkdtemp(folder)) return 1;
    char source[4096], destination[4096], alias[4096], alias_destination[4096];
    path(source, sizeof source, folder, "source"); path(destination, sizeof destination, folder, "clone"); path(alias, sizeof alias, folder, "alias");
    path(alias_destination, sizeof alias_destination, folder, "alias-clone");
    int fd = open(source, O_CREAT | O_EXCL | O_RDWR, 0600), failures = 0;
    const uint64_t size = 64ull << 20;
    uint8_t bytes[16384], readback[16384];
    for (size_t i = 0; i < sizeof bytes; ++i) bytes[i] = (uint8_t)(i * 7 + 3);
    if (fd < 0 || ftruncate(fd, size) || io_all(fd, bytes, sizeof bytes, 0, 1) ||
        io_all(fd, bytes, sizeof bytes, (32ull << 20) + 4096, 1) || io_all(fd, bytes, sizeof bytes, size - sizeof bytes, 1)) failures++;
    if (fd >= 0) close(fd);
    if (!failures && clone_file(source, destination, size)) failures++;
    /* Verify every byte, including all sparse holes, not just the written islands. */
    int a = open(source, O_RDONLY), b = open(destination, O_RDWR);
    for (uint64_t at = 0; !failures && at < size; at += sizeof bytes)
        if (verify_span(a, b, at, sizeof bytes, bytes, readback)) failures++;
    memset(bytes, 0xa5, sizeof bytes);
    if (!failures && (io_all(b, bytes, sizeof bytes, 0, 1) || io_all(a, readback, sizeof readback, 0, 0))) failures++;
    if (!failures) for (size_t i = 0; i < sizeof readback; ++i) if (readback[i] != (uint8_t)(i * 7 + 3)) { failures++; break; }
    if (a >= 0) close(a); if (b >= 0) close(b);
    failures += clone_file(source, destination, size) == 0; /* Never replace an existing clone. */
    b = open(destination, O_RDONLY);
    if (b < 0 || io_all(b, readback, sizeof readback, 0, 0)) failures++;
    else for (size_t i = 0; i < sizeof readback; ++i) if (readback[i] != 0xa5) { failures++; break; }
    if (b >= 0) close(b);
    if (symlink(source, alias) || clone_file(alias, alias_destination, size) == 0) failures++;
    unlink(alias_destination); unlink(alias); unlink(destination); unlink(source);
    if (!failures) failures += pair_test(folder);
    rmdir(folder);
    printf("CLONE_TEST failures=%d sparse_holes=all source_preserved=1 overwrite_rejected=1 symlink_rejected=1 pair_marker_checked=1 partial_pair_cleanup_checked=1\n", failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--selftest")) return selftest(argv[2]);
    if (argc == 3) return clone_disks(argv[1], argv[2]);
    fprintf(stderr, "usage: henge_clone <source-dir> <new-destination-dir> | --selftest <scratch-parent>\n");
    return 2;
}
