#include "frame_transport.h"
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint32_t color(unsigned n) { return 0xff000000u | (n & 255) << 16 | ((n >> 8) & 255) << 8 | ((n ^ 0x5a) & 255); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int produce(const char *path, uint64_t epoch, unsigned count, unsigned delay, int large, int service) {
    LabFrames *f = lab_frames_create(path, epoch);
    if (!f) { perror("frames create"); return 1; }
    uint32_t *pixels = malloc(1280*800*4);
    if (!pixels) { lab_frames_close(f); return 1; }
    double pressed_at = 0;
    for (unsigned n = 1; n <= count; ++n) {
        if (service) {
            struct pollfd input = {.fd=STDIN_FILENO, .events=POLLIN};
            if (poll(&input, 1, 0) > 0 && (input.revents & POLLIN)) {
                char line[32];
                if (fgets(line, sizeof line, stdin)) {
                    if (!strcmp(line, "stop\n")) { puts("FRAME_SERVICE_STOP accepted=1"); break; }
                    unsigned tx,ty;
                    if (sscanf(line,"tap_at %u %u",&tx,&ty)==2 && tx<=65535 && ty<=65535)
                        printf("FRAME_INPUT_TAP_AT x=%u y=%u\n",tx,ty);
                    if (!strcmp(line, "button 0 1\n")) { pressed_at=now(); puts("FRAME_INPUT_BUTTON down=1"); }
                    if (!strcmp(line, "button 0 0\n")) {
                        printf("FRAME_INPUT_BUTTON down=0 matched=%d held_ms=%.0f\n", pressed_at > 0, pressed_at > 0 ? (now()-pressed_at)*1000 : 0);
                        pressed_at=0;
                    }
                }
            }
        }
        unsigned w = (n / 30) % 2 ? 111 : 65, h = w == 111 ? 63 : 49;
        if (large) { w = (n / 30) % 2 ? 1280 : 1024; h = w == 1280 ? 800 : 768; }
        if (service) { w = 1280; h = 800; }
        for (unsigned i = 0; i < w*h; ++i) pixels[i] = color(n);
        if (lab_frames_publish(f, pixels, w, h) != 1) { free(pixels); lab_frames_close(f); return 2; }
        if (delay) usleep(delay);
    }
    if (delay) usleep(250000);
    printf("FRAME_PRODUCER_RESULT frames=%" PRIu64 "\n", lab_frames_published(f));
    free(pixels); lab_frames_close(f); return 0;
}
static int selftest(const char *directory) {
    char folder[4096], path[4120], link[4120];
    int length = snprintf(folder, sizeof folder, "%s/henge-frame-test-XXXXXX", directory);
    if (length < 0 || (size_t)length >= sizeof folder || !mkdtemp(folder)) return 1;
    snprintf(path, sizeof path, "%s/live.bin", folder); snprintf(link, sizeof link, "%s/link.bin", folder);
    int failed = 0;
    LabFrames *writer = lab_frames_create(path, 100);
    if (!writer) return 1;
    LabFrames *reader = lab_frames_open(path, 100);
    LabFrames *bad = lab_frames_open(path, 101);
    if (!reader || bad) failed++;
    lab_frames_close(bad);
    uint32_t pixel = 0xff123456;
    if (lab_frames_publish(writer, &pixel, 4097, 1) != -1 ||
            lab_frames_publish(writer, &pixel, 0, 1) != -1 ||
            lab_frames_publish(writer, &pixel, 1, 1) != 1) failed++;
    LabFrameView view;
    if (lab_frames_take(reader, &view) != 1 || *(const uint32_t *)view.bgra != pixel ||
            lab_frames_take(reader, &view) != -1) failed++;
    lab_frames_close(reader); /* Closing a claimed frame returns the slot. */
    lab_frames_close(writer);
    if (symlink(path, link)) failed++;
    bad = lab_frames_open(link, 0); if (bad) failed++;
    lab_frames_close(bad);
    bad = lab_frames_create(link, 100); if (bad) failed++;
    lab_frames_close(bad); unlink(link);
    FILE *short_file = fopen(link, "wb");
    if (!short_file) failed++; else { fputs("bad", short_file); fclose(short_file); }
    bad = lab_frames_open(link, 0); if (bad) failed++;
    lab_frames_close(bad); unlink(link);
    /* A new inode/epoch distinguishes a restarted writer from the previous run. */
    writer = lab_frames_create(path, 200); if (!writer) failed++;
    lab_frames_close(writer);
    pid_t child = fork();
    if (child == 0) _exit(produce(path, 300, 3000, 25, 0, 0));
    if (child < 0) return 1;
    reader = NULL;
    uint64_t previous = 0; unsigned received = 0, modes = 0, prior_width = 0;
    double deadline = now() + 15;
    while (now() < deadline && previous < 3000) {
        if (!reader) reader = lab_frames_open(path, 300);
        if (!reader) { usleep(1000); continue; }
        int result = lab_frames_take(reader, &view);
        if (result < 0) { failed++; break; }
        if (!result) { usleep(100); continue; }
        if (view.sequence <= previous) failed++;
        uint32_t expected = color((unsigned)view.sequence);
        if (prior_width != view.width) { prior_width = view.width; modes++; }
        /* Hold a frame while the other process writes: this exact buffer must stay immutable. */
        if (received % 17 == 0) usleep(1000);
        for (size_t i = 0; i < (size_t)view.width * view.height; ++i)
            if (((const uint32_t *)view.bgra)[i] != expected) { failed++; break; }
        previous = view.sequence; received++;
        lab_frames_release(reader, &view);
    }
    int status = 0; waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) || previous != 3000 || received < 10 || modes < 2) failed++;
    lab_frames_close(reader); unlink(path); rmdir(folder);
    printf("FRAME_TRANSPORT_RESULT failures=%d received=%u last=%" PRIu64 " modes=%u\n", failed, received, previous, modes);
    return failed ? 1 : 0;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    if (argc == 4 && (!strcmp(argv[1], "--produce") || !strcmp(argv[1], "--produce-large") || !strcmp(argv[1], "--produce-service") || !strcmp(argv[1], "--produce-service-long"))) {
        int service = !strcmp(argv[1], "--produce-service") || !strcmp(argv[1], "--produce-service-long");
        unsigned count = !strcmp(argv[1], "--produce-service-long") ? 7500 : service ? 1000 : 150;
        return produce(argv[2], strtoull(argv[3], NULL, 10), count, 16000, !strcmp(argv[1], "--produce"), service);
    }
    if (argc == 2) return selftest(argv[1]);
    fprintf(stderr, "Usage: frames_test directory | --produce path epoch_ms\n"); return 2;
}
