#include "xhci.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void hv_machine_wake(HvMachine *m) { (void)m; }
int main(void) {
    HvXhci *x=calloc(1,sizeof *x);assert(x);pthread_mutex_init(&x->mu,NULL);
    hv_xhci_key(x,0xe1,1); // Pretend the guest consumed Shift-down.
    x->rhead=x->rtail;
    for(int i=0;i<600;i++) hv_xhci_key(x,4,(i+1)%2);
    assert(x->rtail-x->rhead==HV_XHCI_REPORTS);
    hv_xhci_key(x,0xe1,0);
    uint8_t zero[8]={0};
    assert(!memcmp(x->reports[(x->rtail-1)%HV_XHCI_REPORTS],zero,8));
    hv_xhci_key(x,0xe0,1);
    hv_xhci_release_keys(x);
    assert(x->rtail-x->rhead==1);
    assert(!memcmp(x->state,zero,8) && !memcmp(x->reports[x->rhead%HV_XHCI_REPORTS],zero,8));
    uint32_t tail=x->rtail;
    for(int i=4;i<=255;i++) hv_xhci_key(x,(uint8_t)i,0);
    assert(tail==x->rtail); // Unchanged releases must not fill the report queue.
    x->rhead=x->rtail=UINT32_MAX;
    hv_xhci_key(x,4,1);hv_xhci_release_keys(x);
    assert(x->rtail-x->rhead==1 && !memcmp(x->reports[x->rhead%HV_XHCI_REPORTS],zero,8));
    hv_xhci_release_absolute(x); assert(x->tp_rhead==x->tp_rtail);
    for(int i=0;i<HV_TP_REPORTS;i++) hv_xhci_absolute(x,123,456,1);
    assert(x->tp_rtail-x->tp_rhead==HV_TP_REPORTS);
    hv_xhci_absolute(x,123,456,0);
    assert(x->tp_reports[(x->tp_rtail-1)%HV_TP_REPORTS][0]==0);
    // An up state may still be queued while the guest holds the previous down.
    x->absolute_delivered_buttons=1;
    hv_xhci_release_absolute(x);
    assert(x->tp_rtail-x->tp_rhead==1 && x->tp_rlen[x->tp_rhead%HV_TP_REPORTS]==5);
    assert(x->tp_reports[x->tp_rhead%HV_TP_REPORTS][0]==0);
    assert(!memcmp(x->tp_reports[x->tp_rhead%HV_TP_REPORTS]+1,x->absolute_report+1,4));
    x->absolute_delivered_buttons=0; x->tp_rhead=x->tp_rtail;
    hv_xhci_release_absolute(x); assert(x->tp_rhead==x->tp_rtail);
    pthread_mutex_destroy(&x->mu);free(x);
    puts("KEYBOARD_QUEUE_TEST failures=0 overload_release=pass atomic_reset=pass duplicate_release=pass wrap=pass absolute_release=pass no_spurious_reposition=pass");
}
