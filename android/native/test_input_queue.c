#include "virtio_input_queue.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    HvVirtioInput *d=calloc(1,sizeof *d); assert(d);
    for (int kind=HV_VINPUT_KEYBOARD; kind<=HV_VINPUT_MOUSE; kind++) {
        d->kind=kind; d->head=d->tail=0; d->dropped=d->recoveries=0;
        // Start with guest keys/buttons held, then consume nothing during flood.
        int held[512]; for (unsigned i=0;i<512;i++) held[i]=1;
        for (unsigned i=0;i<HV_VINPUT_RING;i++) hv_vinput_enqueue(d,HV_EV_REL,HV_REL_X,1);
        hv_vinput_enqueue(d,HV_EV_SYN,0,0);
        assert(d->recoveries==1 && d->dropped==HV_VINPUT_RING);
        assert(d->tail-d->head<HV_VINPUT_RING);
        int syn=0;
        while(d->head!=d->tail) {
            unsigned i=d->head++%HV_VINPUT_RING;
            if(d->ring[i].type==HV_EV_KEY) held[d->ring[i].code]=d->ring[i].value!=0;
            if(d->ring[i].type==HV_EV_SYN) syn++;
        }
        if(kind==HV_VINPUT_MOUSE) for(int b=HV_BTN_LEFT;b<=HV_BTN_MIDDLE;b++) assert(!held[b]);
        else for(int key=1;key<256;key++) assert(!held[key]);
        assert(syn>=1);
        // Unsigned wraparound retains the same capacity/recovery contract.
        d->head=d->tail=UINT32_MAX-100;
        for(unsigned i=0;i<HV_VINPUT_RING+1;i++) hv_vinput_enqueue(d,HV_EV_SYN,0,0);
        assert(d->recoveries==2 && d->tail-d->head<HV_VINPUT_RING);
    }
    free(d); puts("INPUT_RING_TEST failures=0 mouse_release=pass keyboard_release=pass wrap=pass");
}
