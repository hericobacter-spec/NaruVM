#include "pointer_control.h"
#include <assert.h>
#include <stdio.h>
static unsigned simulate(double gain, unsigned lag) {
    LabPoint p={0}; assert(!lab_point_enqueue(&p,56000,49000,1));
    int x=100,y=100,dx=0,dy=0,down=0,done=0; uint64_t ack=0,pressed=0; unsigned moves=0;
    for (uint64_t t=1000;t<4000000;t+=1000) {
        if (ack && t>=ack) {
            x+=(int)llround(dx*gain); y+=(int)llround(dy*gain);
            if(x<0)x=0; if(x>1023)x=1023; if(y<0)y=0; if(y>767)y=767; ack=0;
        }
        LabPointAction a=lab_point_step(&p,t,1024,768,1,x,y);
        assert(!(a.flags&LAB_POINT_FAILED));
        if(a.flags&LAB_POINT_MOVE) {assert(!ack); ack=t+lag;assert(a.dx>=-100 && a.dx<=100 && a.dy>=-100 && a.dy<=100);dx=(int8_t)a.dx;dy=(int8_t)a.dy;moves++;}
        if(a.flags&LAB_POINT_DOWN) {assert(!down); assert(abs(x-874)<=2 && abs(y-573)<=2);down=1;pressed=t;}
        if(a.flags&LAB_POINT_UP) {assert(down && t-pressed>=120000);down=0;}
        if(a.flags&LAB_POINT_DONE) {done=1;break;}
    }
    assert(done && !down); return moves;
}
int main(void) {
    assert(lab_point_absolute_span(0,0,127,1024)==1023);
    assert(lab_point_absolute_span(127,32767,127,1024)==1023);
    assert(lab_point_absolute_span(1023,8192,40,1024)==1023);
    assert(lab_point_absolute_span(1023,32767,1023,1024)==1023);
    assert(lab_point_absolute_span(767,32767,127,768)==767);
    assert(fabs(lab_point_absolute_span(1023,16384,512,1024)-1023.96875)<.001);
    unsigned total=0;
    for(unsigned g=0;g<4;g++) for(unsigned l=0;l<3;l++) total+=simulate((double[]){.5,1,2,4}[g],(unsigned[]){10000,60000,180000}[l]);
    LabPoint p={0};assert(!lab_point_enqueue(&p,40000,40000,1));
    unsigned failed=0;
    for(uint64_t t=1;t<=8100001;t+=10000) {LabPointAction a=lab_point_step(&p,t,1024,768,0,0,0);assert(!(a.flags&LAB_POINT_DOWN));failed+=(a.flags&LAB_POINT_FAILED)!=0;}
    assert(failed==1);
    for(unsigned i=0;i<8;i++)assert(!lab_point_enqueue(&p,i,i,1));assert(lab_point_enqueue(&p,1,1,1));
    assert(!lab_point_cancel(&p));assert(p.head==p.tail);
    assert(!lab_point_enqueue(&p,0,0,1));assert(lab_point_step(&p,100,1024,768,1,0,0).flags==LAB_POINT_DOWN);assert(lab_point_cancel(&p));
    printf("POINTER_CONTROL_TEST cases=12 moves=%u timeout_no_click=pass queue_bound=pass cancel_release=pass failures=0\n",total);
}
