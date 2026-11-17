#ifndef HENGE_VIRTIO_INPUT_QUEUE_H
#define HENGE_VIRTIO_INPUT_QUEUE_H
#include "virtio_input.h"

/* Caller holds d->mu. Also used by the host-only overload fixture. */
static inline void hv_vinput_append(HvVirtioInput *d, uint16_t type, uint16_t code, int32_t value) {
    uint32_t i=d->tail++ % HV_VINPUT_RING;
    d->ring[i].type=type; d->ring[i].code=code; d->ring[i].value=(uint32_t)value;
}
static inline void hv_vinput_reset(HvVirtioInput *d) {
        // Dropping a single oldest event can strand a delivered key/button down.
        // Explicit overload recovery abandons stale reports and first resets
        // guest state at a complete SYN boundary. This is not lossless replay.
        d->dropped+=d->tail-d->head; d->recoveries++;
        d->head=d->tail=0;
        if (d->kind == HV_VINPUT_MOUSE) {
            hv_vinput_append(d,HV_EV_REL,HV_REL_X,0);
            hv_vinput_append(d,HV_EV_REL,HV_REL_Y,0);
            hv_vinput_append(d,HV_EV_REL,HV_REL_HWHEEL,0);
            hv_vinput_append(d,HV_EV_REL,HV_REL_WHEEL,0);
            for (int b=HV_BTN_LEFT; b<=HV_BTN_MIDDLE; b++) hv_vinput_append(d,HV_EV_KEY,b,0);
        } else {
            for (int key=1; key<256; key++) hv_vinput_append(d,HV_EV_KEY,key,0);
        }
        hv_vinput_append(d,HV_EV_SYN,0,0);
}
static inline void hv_vinput_enqueue(HvVirtioInput *d, uint16_t type, uint16_t code, int32_t value) {
    if (d->tail-d->head == HV_VINPUT_RING) hv_vinput_reset(d);
    hv_vinput_append(d,type,code,value);
}
#endif
