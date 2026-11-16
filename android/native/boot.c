#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "dispatch.h"
#include "platform.h"
#include "vmapple.h"
#include "dfu.h"
#include "usb_host.h"

static HvMachine machine;
static HvVmapple board;
static HvCpu cpus[1];

/* Firmware execution only: a restore stack and installed macOS disk are not included. */
int lab_boot_firmware(const char *rom, unsigned ram_mb, unsigned seconds, int probe_dfu) {
    if (ram_mb < 256 || ram_mb > 4096 || !seconds || seconds > 120) return 2;
    setenv("HV_RAM_NO_LEDGER", "1", 1);
    setenv("HV_NO_SOUND", "1", 1);
    setenv("HV_PVG_NO_METAL", "1", 1);
    if (hv_machine_init(&machine, 64u << 20)) { perror("machine init"); return 1; }
    machine.log = 1;
    HvCfgParams cfg = {.nr_cpus = 1, .ecid = 0x48454e4745414e44ull,
                       .run_installer1 = 1, .run_installer2 = 1, .seed = 1};
    if (hv_vmapple_create(&board, &machine, cpus, (uint64_t)ram_mb << 20, NULL, NULL, &cfg, NULL)) {
        fprintf(stderr, "BOARD_FAILED errno=%d\n", errno);
        hv_vmapple_destroy(&board); hv_machine_free(&machine); return 1;
    }
    if (hv_vmapple_load_rom(&board, rom)) {
        fprintf(stderr, "ROM_READ_FAILED %s\n", rom);
        hv_vmapple_destroy(&board); hv_machine_free(&machine); return 1;
    }
    hv_vmapple_reset_cpu(&board);
    LabDfuProbe probe = {.usb = board.vusb, .ecid = cfg.ecid};
    pthread_t host_thread;
    if (probe_dfu) {
        hv_libusb_shim_attach(board.vusb);
        int err = pthread_create(&host_thread, NULL, lab_dfu_probe, &probe);
        if (err) {
            fprintf(stderr, "DFU_THREAD_FAILED code=%d\n", err);
            hv_vmapple_destroy(&board); hv_machine_free(&machine); return 1;
        }
    }
    uint64_t start = hv_host_ticks(), until = start + (uint64_t)seconds * 24000000;
    uint64_t blocks = 0, last_log = start;
    printf("ROM_EXPERIMENT ram_mb=%u seconds=%u mode=DFU no_macOS_disk=1 graphics=unavailable\n", ram_mb, seconds);
    int exit_reason = 0;
    for (;;) {
        exit_reason = hv_run_one(&cpus[0]);
        blocks++;
        uint64_t now = hv_host_ticks();
        if (now - last_log >= 24000000) {
            printf("ROM_PROGRESS seconds=%.1f blocks=%" PRIu64 " pc=%" PRIx64 " el=%u\n",
                   (now - start) / 24e6, blocks, cpus[0].pc, cpus[0].el);
            last_log = now;
        }
        if (exit_reason != HV_EXIT_BRANCH || now >= until || (probe_dfu && atomic_load(&probe.done))) break;
    }
    printf("ROM_RESULT blocks=%" PRIu64 " translated=%d exit=%d halt=%u pc=%" PRIx64
           " esr=%" PRIx64 " far=%" PRIx64 "\n", blocks, machine.nblocks, exit_reason,
           cpus[0].halted, cpus[0].pc, cpus[0].esr_el1, cpus[0].far_el1);
    printf("This records firmware execution only; it does not establish a macOS kernel or desktop boot.\n");
    if (probe_dfu) {
        /* On an engine failure/deadline, receive timeouts release the probe. */
        pthread_join(host_thread, NULL);
        hv_libusb_shim_attach(NULL);
    }
    hv_vmapple_destroy(&board);
    hv_machine_free(&machine);
    /* A time-bounded run without an engine error is a completed experiment, not an OS success claim. */
    return exit_reason == HV_EXIT_BRANCH && (!probe_dfu || probe.result == 0) ? 0 : 1;
}
