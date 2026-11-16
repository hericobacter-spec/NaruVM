#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { int axes[4], seen, x,y,buttons,reports; } HvVirtioInput;
enum {HV_EV_SYN=0,HV_EV_KEY=1,HV_EV_REL=2,HV_REL_X=0,HV_REL_Y=1,HV_REL_HWHEEL=6,HV_REL_WHEEL=8,HV_BTN_LEFT=272};
static void hv_virtio_input_send(HvVirtioInput *d,int type,int code,int value) {
    if(type==HV_EV_REL) {
        int i=code==0?0:code==1?1:code==6?2:3;
        assert(value>=-100 && value<=100);
        d->axes[i]=(int8_t)value;d->seen|=1<<i;
    } else if(type==HV_EV_KEY) {
        if(value)d->buttons|=1<<(code-HV_BTN_LEFT);else d->buttons&=~(1<<(code-HV_BTN_LEFT));
    } else {
        assert(d->seen==15);d->seen=0;
        /* Deliberately retain axes after SYN, reproducing the stale-axis risk. */
        d->x+=d->axes[0];d->y+=d->axes[1];d->reports++;
    }
}
#include "mouse_packets.h"
int main(void) {
    HvVirtioInput d={0};lab_mouse_move(&d,370,320);assert(d.x==370 && d.y==320);
    lab_mouse_frame(&d,0,0,0,0,0,1);assert(d.x==370 && d.y==320 && d.buttons==1);
    lab_mouse_frame(&d,0,0,0,0,0,0);assert(d.x==370 && d.y==320 && !d.buttons);
    lab_mouse_frame(&d,0,0,0,4,-1,0);assert(d.x==370 && d.y==320);
    lab_mouse_move(&d,-32767,32767);assert(d.x==370-32767 && d.y==320+32767);
    lab_mouse_move(&d,0,0);assert(d.axes[2]==0 && d.axes[3]==0);
    puts("MOUSE_PACKETS_TEST signed8_motion=pass stationary_click=pass wheel_isolation=pass full_range=pass");
}
