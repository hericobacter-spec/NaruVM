#ifndef HENGE_POINTER_CONTROL_H
#define HENGE_POINTER_CONTROL_H
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
/* Bounded feedback positioning over the guest's existing relative mouse. No click is
 * sent until the display reports that the pointer reached the requested location. */
typedef struct { unsigned x, y; int click; } LabPointTarget;
typedef struct {
    LabPointTarget queue[8]; unsigned head, tail;
    int active, down, sent, last_x, last_y, sent_x, sent_y, steps;
    double gain_x, gain_y;
    uint64_t began, due;
} LabPoint;
enum { LAB_POINT_MOVE=1, LAB_POINT_DOWN=2, LAB_POINT_UP=4, LAB_POINT_DONE=8, LAB_POINT_FAILED=16 };
typedef struct { unsigned flags; int dx, dy; } LabPointAction;
/* A stale cursor sample must not collapse a full desktop into a tiny span.
 * Start from the observed scanout extent; only admit nearby, unclipped scales. */
static inline double lab_point_absolute_span(double prior, unsigned report, int feedback, unsigned extent) {
    if (extent < 2) return 0;
    double base=(double)(extent-1);
    if (prior < base*.5 || prior > base*2.) prior=base;
    if (report <= 256 || feedback <= 2 || feedback >= (int)extent-3) return prior;
    double sample=(double)feedback*32767/report;
    if (sample >= base*.5 && sample <= base*2.) return sample;
    return prior;
}
static inline int lab_point_enqueue(LabPoint *p, unsigned x, unsigned y, int click) {
    if (p->tail-p->head == 8) return -1;
    /* Unconsumed motion is replaceable; button transactions are never coalesced. */
    if (!click && p->tail != p->head && !p->queue[(p->tail-1)%8].click &&
            (!p->active || p->tail-p->head > 1)) p->tail--;
    p->queue[p->tail++%8]=(LabPointTarget){x,y,click}; return 0;
}
static inline int lab_point_cancel(LabPoint *p) {
    int release=p->down; *p=(LabPoint){0}; return release;
}
static inline int lab_point_delta(int delta, double gain) {
    if (!delta) return 0;
    int v=(int)llround(delta/gain);
    if (!v) v=delta>0 ? 1 : -1;
    if (v>100) v=100;
    if (v< -100) v= -100;
    return v;
}
static inline LabPointAction lab_point_step(LabPoint *p, uint64_t now,
        unsigned width, unsigned height, int valid, int x, int y) {
    LabPointAction a={0};
    if (p->head==p->tail) return a;
    if (!p->active) {
        p->active=1; p->began=now; p->due=now; p->sent=0; p->steps=0;
        p->gain_x=p->gain_y=2.;
    }
    if (now<p->due) return a;
    if (p->down) {
        p->down=0; p->active=0; p->head++; a.flags=LAB_POINT_UP|LAB_POINT_DONE; return a;
    }
    if (now-p->began>=8000000) {
        p->active=0; p->head++; a.flags=LAB_POINT_FAILED; return a;
    }
    if (!valid || !width || !height) return a;
    LabPointTarget t=p->queue[p->head%8];
    int tx=(int)(((uint64_t)t.x*(width-1)+32767)/65535);
    int ty=(int)(((uint64_t)t.y*(height-1)+32767)/65535);
    if (abs(tx-x)<=2 && abs(ty-y)<=2) {
        if (t.click) { p->down=1; p->due=now+120000; a.flags=LAB_POINT_DOWN; }
        else { p->active=0; p->head++; a.flags=LAB_POINT_DONE; }
        return a;
    }
    if (p->sent) {
        if (x==p->last_x && y==p->last_y) return a; /* wait for guest feedback */
        double gx=p->sent_x ? (double)(x-p->last_x)/p->sent_x : 0;
        double gy=p->sent_y ? (double)(y-p->last_y)/p->sent_y : 0;
        if (gx>=.25 && gx<=8.) p->gain_x=gx;
        if (gy>=.25 && gy<=8.) p->gain_y=gy;
    }
    a.dx=lab_point_delta(tx-x,p->gain_x); a.dy=lab_point_delta(ty-y,p->gain_y);
    p->last_x=x; p->last_y=y; p->sent_x=a.dx; p->sent_y=a.dy;
    p->sent=1; p->steps++; p->due=now+25000; a.flags=LAB_POINT_MOVE; return a;
}
#endif
