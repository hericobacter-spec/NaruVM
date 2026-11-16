#include <stdatomic.h>
#include <stdint.h>
#include "virtio_usb.h"

typedef struct {
    HvVirtioUsb *usb;
    uint64_t ecid;
    atomic_int done;
    int result;
} LabDfuProbe;

void *lab_dfu_probe(void *opaque);
