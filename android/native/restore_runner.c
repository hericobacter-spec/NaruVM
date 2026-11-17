#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include "disk.h"
#include "dispatch.h"
#include "install.h"
#include "platform.h"
#include "cpu_performance.h"
#include "vmapple.h"
#include "reims_gpu.h"
#include "installed_guard.h"
#include "vm_control.h"
#include "pointer_control.h"
#include "net_slirp.h"
#include "dns_proxy.h"
#include "jit_profile.h"

static const uint8_t aux_header[24] = {0xa5, 0xd5, 0xb3, 0xfe, 0x97, 0xad, 0x1d, 0x27,
                                      1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
static uint64_t ecid = 0x48454e4745414e44ull;
static HvMachine m;
static HvVmapple board;
static HvCpu cpu[4];
extern int lab_thread_diagnostics(void);
static HvDisk root = {.fd = -1, .wfd = -1}, aux = {.fd = -1, .wfd = -1};
static atomic_int stop;
static uint64_t deadline, started;
static int timed_out;
static int restoring;
static int interactive, requested_stop;
static uint64_t input_accepted, input_rejected;
static LabPoint pointing;
#include "mouse_packets.h"
static unsigned absolute_x, absolute_y;
static double absolute_span_x, absolute_span_y;
static int absolute_buttons, absolute_ready, button_uses_absolute;
static void point_button(int down) {
    if (down) button_uses_absolute=absolute_ready;
    if (button_uses_absolute) {
        absolute_buttons=down ? absolute_buttons|1 : absolute_buttons&~1;
        hv_xhci_absolute(board.usb,absolute_x,absolute_y,absolute_buttons);
    } else {
        /* A relative-device click preserves its current position, including
         * before the first absolute move or after trackpad motion. */
        lab_mouse_frame(board.mouse,0,0,0,0,0,down);
    }
}
static void cancel_pointing(void) { if (lab_point_cancel(&pointing)) point_button(0); }
static void poll_pointing(void) {
    if (pointing.head==pointing.tail) return;
    unsigned w=0,h=0; int x=0,y=0;
    int valid=hv_android_reims_pointer(board.android_reims,&w,&h,&x,&y);
    uint64_t now=hv_host_ticks()/24;
    LabPointAction a=lab_point_step(&pointing,now,w,h,valid,x,y);
    if (a.flags & LAB_POINT_MOVE) {
        LabPointTarget target=pointing.queue[pointing.head%8];

        /* The guest's HID desktop bounds may differ from its scanout mode.
         * Calibrate from the previous absolute report and actual cursor feedback,
         * not from a fixed tablet/model resolution. Avoid clipped edge samples. */
        absolute_span_x=lab_point_absolute_span(absolute_span_x,
            pointing.steps>1 ? absolute_x : 0,x,w);
        absolute_span_y=lab_point_absolute_span(absolute_span_y,
            pointing.steps>1 ? absolute_y : 0,y,h);
        unsigned tx=(unsigned)(((uint64_t)target.x*(w-1)+32767)/65535);
        unsigned ty=(unsigned)(((uint64_t)target.y*(h-1)+32767)/65535);
        absolute_x=absolute_span_x ? (unsigned)fmin(32767,fmax(0,llround(tx*32767/absolute_span_x))) : 8192;
        absolute_y=absolute_span_y ? (unsigned)fmin(32767,fmax(0,llround(ty*32767/absolute_span_y))) : 8192;
        printf("VM_POINT_SAMPLE frame=%ux%u feedback=%d,%d target=%u,%u report=%u,%u\n",w,h,x,y,tx,ty,absolute_x,absolute_y);
        hv_xhci_absolute(board.usb,absolute_x,absolute_y,absolute_buttons);
        absolute_ready=1;
    }
    if (a.flags & LAB_POINT_DOWN) point_button(1);
    if (a.flags & LAB_POINT_UP) point_button(0);
    if (a.flags & (LAB_POINT_DONE|LAB_POINT_FAILED))
        printf("VM_POINT_RESULT reached=%d steps=%d elapsed_us=%" PRIu64 " span_x=%.1f span_y=%.1f\n",!(a.flags&LAB_POINT_FAILED),pointing.steps,now-pointing.began,absolute_span_x,absolute_span_y);
}

static void control_line(const char *line) {
    LabControl c;
    if (lab_control_parse(line, &c)) { input_rejected++; return; }
    input_accepted++;
    if (restoring && c.kind!=LAB_CONTROL_STOP) { input_rejected++; input_accepted--; return; }
    if (c.kind==LAB_CONTROL_STOP || c.kind==LAB_CONTROL_MOVE || c.kind==LAB_CONTROL_BUTTON || c.kind==LAB_CONTROL_RELEASE_ALL) cancel_pointing();
    switch (c.kind) {
    case LAB_CONTROL_STOP:
        requested_stop = 1; hv_machine_halt(&m, HV_HALT_HOST); break;
    case LAB_CONTROL_RELEASE_ALL:
        hv_xhci_release_keys(board.usb);
        absolute_buttons=0;
        hv_xhci_release_absolute(board.usb);
        hv_virtio_input_release_all(board.mouse);
        printf("VM_INPUT_STATE_RESET\n"); break;
    case LAB_CONTROL_HID: hv_xhci_key(board.usb, (uint8_t)c.a, c.b); break;
    case LAB_CONTROL_MOVE:
        absolute_ready=0;
        lab_mouse_move(board.mouse,c.a,c.b); break;
    case LAB_CONTROL_BUTTON:
        if (c.a==0) point_button(c.b);
        else lab_mouse_frame(board.mouse,0,0,0,0,c.a,c.b); break;
    case LAB_CONTROL_POINT:
    case LAB_CONTROL_TAP_AT:
        if (lab_point_enqueue(&pointing,(unsigned)c.a,(unsigned)c.b,c.kind==LAB_CONTROL_TAP_AT)) {
            input_rejected++; input_accepted--; printf("VM_POINT_QUEUE_FULL\n");
        }
        break;
    case LAB_CONTROL_SCROLL:
        lab_mouse_frame(board.mouse,0,0,c.a,c.b,-1,0); break;
    }
    if (input_accepted <= 12) printf("VM_CONTROL accepted=%" PRIu64 " kind=%d\n", input_accepted, c.kind);
}

static void poll_control(void) {
    static char line[128];
    static unsigned used;
    static int discard;
    struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
    if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN)) return;
    char bytes[1024];
    ssize_t n = read(STDIN_FILENO, bytes, sizeof bytes);
    for (ssize_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == '\n') {
            if (discard) input_rejected++;
            else { line[used] = 0; control_line(line); }
            used = 0; discard = 0;
        } else if (c < 32 || c > 126 || used == sizeof line - 1) discard = 1;
        else if (!discard) line[used++] = (char)c;
    }
}

static int image(const char *path, uint64_t size, int auxiliary) {
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd >= 0) {
        int ok = ftruncate(fd, (off_t)size) == 0;
        if (ok && auxiliary) ok = pwrite(fd, aux_header, sizeof aux_header, 0) == sizeof aux_header;
        if (ok) ok = fsync(fd) == 0;
        close(fd);
        if (!ok) { perror("image create"); return -1; }
        printf("DISK_CREATED path=%s logical_bytes=%" PRIu64 " sparse=1\n", path, size);
    } else if (errno != EEXIST) { perror("image open"); return -1; }
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != size) {
        fprintf(stderr, "DISK_GEOMETRY_MISMATCH %s (existing data preserved)\n", path); return -1;
    }
    if (auxiliary) {
        uint8_t header[24];
        fd = open(path, O_RDONLY | O_CLOEXEC);
        int ok = fd >= 0 && pread(fd, header, sizeof header, 0) == sizeof header &&
                 !memcmp(header, aux_header, sizeof header);
        if (fd >= 0) close(fd);
        if (!ok) { fprintf(stderr, "AUX_HEADER_MISMATCH\n"); return -1; }
    }
    printf("DISK_READY path=%s logical_bytes=%" PRIu64 " allocated_bytes=%" PRIu64 "\n",
           path, size, (uint64_t)st.st_blocks * 512);
    return 0;
}

int lab_disk_init(const char *dir) {
    if (mkdir(dir, 0700) && errno != EEXIST) { perror("mkdir"); return 1; }
    char a[1024], b[1024];
    if (snprintf(a, sizeof a, "%s/root.raw", dir) >= sizeof a ||
        snprintf(b, sizeof b, "%s/auxiliary.bin", dir) >= sizeof b) return 2;
    return image(a, 64ull << 30, 0) || image(b, 33570816, 1);
}

int lab_disk_test(const char *dir) {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/henge-disk-check-XXXXXX", dir) >= sizeof path) return 2;
    int fd = mkstemp(path);
    if (fd < 0) { perror("disk check create"); return 1; }
    int failures = ftruncate(fd, 128ull << 20) != 0;
    close(fd);
    HvDisk d = {.fd = -1, .wfd = -1};
    uint8_t data[4096], readback[4096];
    memset(data, 0xa5, sizeof data);
    if (hv_disk_open(&d, path, 16384, 0)) failures++;
    else {
        memset(readback, 1, sizeof readback);
        failures += hv_disk_read(&d, 0, readback, sizeof readback) != 0;
        for (unsigned i = 0; i < sizeof readback; i++) if (readback[i]) { failures++; break; }
        failures += hv_disk_write(&d, d.size - sizeof data, data, sizeof data) != 0;
        failures += hv_disk_flush(&d) != 0;
        failures += hv_disk_write(&d, d.size - 1, data, 2) != -1;
        failures += hv_disk_read(&d, UINT64_MAX, readback, 2) != -1;
        hv_disk_close(&d);
        if (hv_disk_open(&d, path, 16384, 1)) failures++;
        else {
            failures += hv_disk_read(&d, d.size - sizeof data, readback, sizeof readback) != 0;
            failures += memcmp(data, readback, sizeof data) != 0;
            failures += hv_disk_write(&d, 0, data, sizeof data) != -1;
            hv_disk_close(&d);
        }
    }
    unlink(path); /* Only the scratch file this call exclusively created. */
    printf("DISK_TEST failures=%d (sparse zeros, offset, durable reopen, bounds, readonly)\n", failures);
    return failures ? 1 : 0;
}

static void progress(int step, double fraction, void *unused) {
    printf("RESTORE_PROGRESS step=%d fraction=%.4f\n", step, fraction);
}

static void *kicker(void *unused) {
    uint64_t last = started, last_profile = started;
    while (!atomic_load(&stop)) {
        usleep(1000);
        if (interactive || restoring) poll_control();
        if (interactive) poll_pointing();
        hv_machine_kick_all(&m);
        uint64_t now = hv_host_ticks();
        if (now - last_profile >= 24000000) {
            lab_jit_profile_finish(&m);
            last_profile = now;
        }
        if (now - last >= 120000000) {
            printf("%s_HEARTBEAT elapsed=%.0f result=%d\n", restoring ? "RESTORE" : "BOOT_DISK",
                   (now - started) / 24e6, restoring ? hv_install_result() : 1);
            if (getenv("HENGE_CPU_PROFILE") && !pthread_mutex_trylock(&m.tr_mu)) {
                printf("JIT_PROFILE_STATE pid=%u generation=%u base=0x%" PRIxPTR " used=%zu blocks=%d capacity=%zu\n",
                    (unsigned)getpid(), m.jit_gen, (uintptr_t)m.jit.base, m.jit.used * 4, m.nblocks, m.jit.capacity * 4);
                pthread_mutex_unlock(&m.tr_mu);
            }
            if (interactive && board.mouse && !pthread_mutex_trylock(&board.mouse->mu)) {
                printf("VM_INPUT_PROGRESS accepted=%" PRIu64 " rejected=%" PRIu64 " mouse_delivered=%" PRIu64 " mouse_queued=%u mouse_dropped=%" PRIu64 "\n",
                    input_accepted, input_rejected, board.mouse->delivered,
                    board.mouse->tail-board.mouse->head, board.mouse->dropped);
                pthread_mutex_unlock(&board.mouse->mu);
            }
            last = now;
        }
        if (requested_stop) break;
        if (now >= deadline || (restoring && hv_install_result() != 1)) {
            timed_out = now >= deadline;
            hv_machine_halt(&m, HV_HALT_HOST);
            break;
        }
    }
    return NULL;
}

int lab_restore(const char *rom, const char *ipsw, const char *dir, unsigned ram_mb, unsigned seconds) {
    restoring = 1;
    if (ram_mb < 1024 || ram_mb > 4096 || (seconds && (seconds < 10 || seconds > 7200))) return 2;
    if (access(rom, R_OK) || access(ipsw, R_OK)) { perror("restore asset"); return 1; }
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) { fprintf(stderr, "CURL_INIT_FAILED\n"); return 1; }
    if (naru_machine_ecid(dir,&ecid) || lab_disk_init(dir)) return 1;
    char path[1024];
    snprintf(path, sizeof path, "%s/root.raw", dir);
    if (hv_disk_open(&root, path, 0, 0)) return 1;
    uint8_t first_sector[4096];
    if (hv_disk_read(&root, 0, first_sector, sizeof first_sector)) return 1;
    for (unsigned i = 0; i < sizeof first_sector; i++) if (first_sector[i]) {
        fprintf(stderr, "RESTORE_EXISTING_DISK_REFUSED: partition data present; preserve/rename the guest directory before a fresh restore\n");
        hv_disk_close(&root); return 1;
    }
    snprintf(path, sizeof path, "%s/auxiliary.bin", dir);
    if (hv_disk_open(&aux, path, 16384, 0)) { hv_disk_close(&root); return 1; }
    setenv("HV_RAM_NO_LEDGER", "1", 1);
    setenv("HV_NO_SOUND", "1", 1);
    setenv("HV_PVG_NO_METAL", "1", 1);
    if (hv_machine_init_cpus(&m, 256u << 20, 1)) { perror("machine init"); return 1; }
    m.log = 1;
    HvCfgParams cfg = {.nr_cpus = 1, .ecid = ecid, .run_installer1 = 1, .run_installer2 = 1, .seed = 1};
    if (hv_vmapple_create(&board, &m, cpu, (uint64_t)ram_mb << 20, &root, &aux, &cfg, NULL) ||
        hv_vmapple_load_rom(&board, rom)) { fprintf(stderr, "RESTORE_BOARD_FAILED\n"); return 1; }
    hv_vmapple_reset_cpu(&board);
    printf("RESTORE_EXPERIMENT ram_mb=%u seconds=%u ecid=0x%016" PRIx64 " graphics=unavailable\n", ram_mb, seconds, ecid);
    started = hv_host_ticks(); deadline = seconds ? started + (uint64_t)seconds * 24000000 : UINT64_MAX;
    if (hv_install_start(board.vusb, ipsw, dir, ecid, progress, NULL)) return 1;
    pthread_t timer;
    if (pthread_create(&timer, NULL, kicker, NULL)) return 1;
    int reason = hv_run(&cpu[0]);
    atomic_store(&stop, 1);
    pthread_join(timer, NULL);
    hv_virtio_blk_stop(board.root_blk);
    hv_virtio_blk_stop(board.aux_blk);
    int flush_failed = hv_disk_flush(&root) || hv_disk_flush(&aux);
    printf("RESTORE_RESULT result=%d timeout=%d engine_exit=%d pc=0x%" PRIx64
           " translated=%d flush_failed=%d elapsed=%.1f\n", hv_install_result(), timed_out, reason,
           cpu[0].pc, m.nblocks, flush_failed, (hv_host_ticks() - started) / 24e6);
    /* install.c has process-lifetime detached USB/restore workers. Do not free
       their board here. main exits this isolated child, releasing them together. */
    return hv_install_result() == 0 && !timed_out && !requested_stop && !flush_failed ? 0 : 1;
}

static void le32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = n >> (8 * i);
}

/* Same boot_args fallback as host/vm.c, read only after the guest is stopped.
   A framebuffer is boot evidence, not evidence of a working desktop. */
static int capture_boot_frame(const char *dir) {
    uint64_t limit = board.ram_size < (1ull << 30) ? board.ram_size : 1ull << 30;
    for (uint64_t off = 0; off + 0x1000 <= limit; off += 0x4000) {
        const uint64_t *q = (const uint64_t *)(board.ram + off);
        const uint16_t *h = (const uint16_t *)q;
        if (h[0] < 2 || h[0] > 3 || h[1] < 1 || h[1] > 3 || q[1] >> 40 != 0xfffffe ||
            q[2] < VMAPPLE_MEM || q[2] - VMAPPLE_MEM >= board.ram_size || !q[3] || q[3] > board.ram_size) continue;
        uint64_t base = q[5], row = q[7], w = q[8], height = q[9];
        if (w < 320 || w > 8192 || height < 200 || height > 8192 ||
            row < w * 4 || row > (1ull << 20) || (q[10] & 255) != 32 || base < VMAPPLE_MEM ||
            base - VMAPPLE_MEM > board.ram_size || row * height > board.ram_size - (base - VMAPPLE_MEM)) continue;
        char path[1024], temp[1024];
        if (snprintf(path, sizeof path, "%s/boot-frame.bmp", dir) >= sizeof path ||
            snprintf(temp, sizeof temp, "%s/boot-frame.bmp.tmp", dir) >= sizeof temp) return -1;
        FILE *f = fopen(temp, "wb");
        if (!f) return -1;
        uint8_t header[54] = {'B', 'M'};
        le32(header + 2, 54 + w * height * 4); le32(header + 10, 54);
        le32(header + 14, 40); le32(header + 18, w); le32(header + 22, -(int32_t)height);
        header[26] = 1; header[28] = 32; le32(header + 34, w * height * 4);
        int ok = fwrite(header, 1, sizeof header, f) == sizeof header;
        const uint8_t *pixels = board.ram + base - VMAPPLE_MEM;
        for (uint64_t y = 0; y < height && ok; y++) ok = fwrite(pixels + y * row, 4, w, f) == w;
        if (fflush(f) || fsync(fileno(f))) ok = 0;
        if (fclose(f)) ok = 0;
        if (!ok || rename(temp, path)) return -1;
        printf("FRAMEBUFFER_CAPTURE boot_args=0x%" PRIx64 " base=0x%" PRIx64
               " width=%" PRIu64 " height=%" PRIu64 " path=%s\n", (uint64_t)(VMAPPLE_MEM + off), base, w, height, path);
        return 1;
    }
    printf("FRAMEBUFFER_UNAVAILABLE no validated boot_args framebuffer found\n");
    return 0;
}


static void *secondary_cpu(void *arg) {
    HvCpu *c = arg;
    if (lab_thread_diagnostics()) {
        perror("secondary sigaltstack"); hv_machine_halt(&m, HV_HALT_HOST); return NULL;
    }
    if (interactive && getenv("HENGE_CPU_PERFORMANCE")) lab_cpu_performance(c->index);
    int reason = hv_run(c);
    printf("BOOT_SECONDARY_RESULT cpu=%u engine_exit=%d halt=%d\n", c->index, reason, c->halted);
    if (!c->halted) hv_machine_halt(&m, HV_HALT_HOST);
    return NULL;
}

static int boot_disk(const char *rom, const char *dir, unsigned ram_mb, unsigned seconds, int reims, unsigned ncpus) {
    if (ncpus < 1 || ncpus > 4) return 2;
    if (ram_mb < 1024 || ram_mb > 4096 || (!(reims && seconds == 0) && (seconds < 10 || seconds > 7200))) return 2;
    if (reims && naru_boot_guard(dir)) { fprintf(stderr, "REIMS_PROTECTED_CLONE_REQUIRED\n"); return 1; }
    char path[1024]; struct stat st;
    if (naru_machine_ecid(dir,&ecid)) return 1;
    if (access(rom, R_OK)) return 1;
    if (snprintf(path, sizeof path, "%s/root.raw", dir) >= sizeof path || stat(path, &st) ||
        !S_ISREG(st.st_mode) || st.st_size != (64ull << 30) || hv_disk_open(&root, path, 0, 0)) {
        fprintf(stderr, "BOOT_DISK_EXISTING_ROOT_REQUIRED\n"); return 1;
    }
    if (snprintf(path, sizeof path, "%s/auxiliary.bin", dir) >= sizeof path || stat(path, &st) ||
        !S_ISREG(st.st_mode) || st.st_size != 33570816 || hv_disk_open(&aux, path, 16384, 0)) {
        fprintf(stderr, "BOOT_DISK_EXISTING_AUX_REQUIRED\n"); hv_disk_close(&root); return 1;
    }
    setenv("HV_RAM_NO_LEDGER", "1", 1); setenv("HV_NO_SOUND", "1", 1); setenv("HV_PVG_NO_METAL", "1", 1);
    unsigned jit_mb = 256;
    const char *jit_setting = getenv("HENGE_JIT_MB");
    if (jit_setting) {
        char *end; unsigned long requested = strtoul(jit_setting, &end, 10);
        if (!*jit_setting || *end || requested < 128 || requested > 512) {
            fprintf(stderr, "BOOT_DISK_INVALID_JIT_MB\n"); return 2;
        }
        jit_mb = (unsigned)requested;
    }
    printf("BOOT_DISK_JIT capacity_mb=%u\n", jit_mb);
    if (getenv("HENGE_CPU_PROFILE")) setenv("HV_JIT_SPANS", "1", 1);
    if (hv_machine_init_cpus(&m, (size_t)jit_mb << 20, ncpus)) return 1;
    printf("JIT_MEMORY_PATH early_addend=%d tag_page_permissions=checked\n", hv_knob[HV_K_EARLY_ADDEND] != 0);
    m.log = 1;
    const char *stats = getenv("HENGE_CPU_STATS");
    if (stats && atoi(stats) >= 1 && atoi(stats) <= 60) m.stats_interval = atoi(stats);
    HvCfgParams cfg = {.nr_cpus = ncpus, .ecid = ecid, .seed = 1};
    static const uint8_t mac[6] = {0x02, 0x48, 0x56, 0x4d, 0x00, 0x01};
    if (hv_vmapple_create(&board, &m, cpu, (uint64_t)ram_mb << 20, &root, &aux, &cfg, mac) ||
        hv_vmapple_load_rom(&board, rom)) return 1;
    int network_on = hv_net_attach(&m, board.net) == 0;
    if (network_on) board.poll = hv_net_poll;
    else hv_virtio_net_set_link(board.net, 0);
    printf("GUEST_NETWORK attached=%d mode=virtio-ethernet-nat dns=android-system inbound_forwarding=0\n", network_on);
    hv_vmapple_reset_cpu(&board);
    printf("BOOT_DISK_EXPERIMENT ram_mb=%u seconds=%u installer=0 graphics=%s cpus=%u\n", ram_mb, seconds,
        reims ? "reims-cpu-vulkan" : "headless", ncpus);
    interactive = reims;
    started = hv_host_ticks(); deadline = seconds ? started + (uint64_t)seconds * 24000000 : UINT64_MAX;
    pthread_t timer, secondary[3];
    unsigned nsecondary = 0;
    if (ncpus > 1) {
        pthread_attr_t attr;
        pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 8u << 20);
        int error = 0;
        for (unsigned i = 1; i < ncpus; ++i) {
            error = pthread_create(&secondary[nsecondary], &attr, secondary_cpu, &cpu[i]);
            if (error) break;
            nsecondary++;
        }
        pthread_attr_destroy(&attr);
        if (error) {
            hv_machine_halt(&m, HV_HALT_HOST);
            for (unsigned i=0; i<nsecondary; ++i) pthread_join(secondary[i], NULL);
            hv_net_detach(); return 1;
        }
    }
    if (pthread_create(&timer, NULL, kicker, NULL)) {
        hv_machine_halt(&m, HV_HALT_HOST);
        for (unsigned i=0; i<nsecondary; ++i) pthread_join(secondary[i], NULL);
        hv_net_detach();
        return 1;
    }
    if (reims && getenv("HENGE_CPU_PERFORMANCE")) lab_cpu_performance(0);
    int reason = hv_run(&cpu[0]);
    hv_machine_halt(&m, HV_HALT_HOST);
    atomic_store(&stop, 1); pthread_join(timer, NULL);
    for (unsigned i=0; i<nsecondary; ++i) pthread_join(secondary[i], NULL);
    lab_jit_profile_finish(&m);
    hv_net_detach();
    uint64_t dns_answered, dns_handed_back, dns_timed_out;
    hv_dns_proxy_stats(&dns_answered, &dns_handed_back, &dns_timed_out);
    printf("GUEST_NETWORK_RESULT tx=%" PRIu64 " rx=%" PRIu64 " dns_answered=%" PRIu64 " dns_failures=%" PRIu64 "\n",
        hv_net_tx_frames, hv_net_rx_frames, dns_answered, dns_timed_out);
    hv_virtio_blk_stop(board.root_blk); hv_virtio_blk_stop(board.aux_blk);
    int flush_failed = hv_disk_flush(&root) || hv_disk_flush(&aux);
    int frame = capture_boot_frame(dir);
    if (reims) {
        hv_android_reims_report(board.android_reims);
        hv_android_reims_destroy(board.android_reims);
        board.android_reims = NULL;
    }
    if (reims) printf("VM_CONTROL_RESULT accepted=%" PRIu64 " rejected=%" PRIu64 " requested_stop=%d keyboard_delivered=%" PRIu64 " mouse_delivered=%" PRIu64 " mouse_dropped=%" PRIu64 "\n",
        input_accepted, input_rejected, requested_stop, board.usb->delivered, board.mouse->delivered, board.mouse->dropped);
    printf("BOOT_DISK_RESULT timeout=%d engine_exit=%d pc=0x%" PRIx64
           " translated=%d panic_events=%" PRIu64 " flush_failed=%d framebuffer=%d elapsed=%.1f\n",
           timed_out, reason, cpu[0].pc, m.nblocks, board.panic_events, flush_failed, frame,
           (hv_host_ticks() - started) / 24e6);
    /* Exit 0 means a controlled bounded experiment, not OS/desktop success. */
    return (timed_out || requested_stop) && !board.panic_events && !flush_failed && frame >= 0 ? 0 : 1;
}

int lab_boot_disk(const char *rom, const char *dir, unsigned ram_mb, unsigned seconds) {
    unsetenv("HV_GPU_BACKEND");
    return boot_disk(rom, dir, ram_mb, seconds, 0, 1);
}

int lab_boot_reims(const char *rom, const char *dir, unsigned ram_mb, unsigned seconds,
                   const char *library, const char *loader, unsigned ncpus) {
    if (naru_boot_guard(dir)) { fprintf(stderr, "REIMS_PROTECTED_CLONE_REQUIRED\n"); return 1; }
    char cache[4096], frame[4096];
    if (library[0] != '/' || loader[0] != '/' ||
        snprintf(cache, sizeof cache, "%s/reims-cache", dir) >= sizeof cache ||
        snprintf(frame, sizeof frame, "%s/reims-frame.bmp", dir) >= sizeof frame) return 2;
    if (mkdir(cache, 0700) && errno != EEXIST) return 1;
    setenv("HV_GPU_BACKEND", "reims", 1);
    setenv("HENGE_REIMS_LIBRARY", library, 1);
    setenv("HENGE_REIMS_FRAME", frame, 1);
    setenv("REIMS_VGPU_SHADER_CACHE_DIR", cache, 1);
    if (!getenv("MESA_SHADER_CACHE_DIR")) setenv("MESA_SHADER_CACHE_DIR", cache, 1);
    setenv("REIMS_VGPU_VULKAN_LOADER", loader, 1);
    return boot_disk(rom, dir, ram_mb, seconds, 1, ncpus);
}
