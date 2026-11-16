#ifndef HENGE_DISK_CLONE_GUARD_H
#define HENGE_DISK_CLONE_GUARD_H
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int clone_guard_path(char *out, size_t capacity, const char *dir, const char *name) {
    int n = snprintf(out, capacity, "%s/%s", dir, name);
    return n > 0 && (size_t)n < capacity;
}
static int hv_research_clone_guard(const char *dir) {
    char path[4096], source[4096], line[8192];
    if (!clone_guard_path(path, sizeof path, dir, "research-clone.txt")) return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat marker;
    if (fstat(fd, &marker) || !S_ISREG(marker.st_mode)) { close(fd); return -1; }
    FILE *f = fdopen(fd, "r");
    if (!f) { close(fd); return -1; }
    int ok = fgets(line, sizeof line, f) && !strcmp(line, "HENGE_RESEARCH_CLONE_V1\n") &&
        fgets(line, sizeof line, f) && !strncmp(line, "source=/", 8);
    if (ok) {
        char *newline = strchr(line, '\n');
        ok = newline != NULL;
        if (newline) *newline = 0;
        size_t length = strlen(line + 7);
        ok = ok && length < sizeof source;
        if (ok) memcpy(source, line + 7, length + 1);
    }
    fclose(f);
    const char *names[] = {"root.raw", "auxiliary.bin"};
    for (unsigned i = 0; ok && i < 2; ++i) {
        struct stat original, clone;
        ok = clone_guard_path(path, sizeof path, source, names[i]) && !lstat(path, &original) &&
            clone_guard_path(path, sizeof path, dir, names[i]) && !lstat(path, &clone) &&
            S_ISREG(original.st_mode) && S_ISREG(clone.st_mode) &&
            (original.st_dev != clone.st_dev || original.st_ino != clone.st_ino);
    }
    if (ok) printf("REIMS_CLONE_GUARD source=%s target=%s distinct_files=1\n", source, dir);
    return ok ? 0 : -1;
}

#endif
