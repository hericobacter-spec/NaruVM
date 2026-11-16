#ifndef HENGE_MOUSE_PACKETS_H
#define HENGE_MOUSE_PACKETS_H
/* Apple guest relative reports can retain axes across SYN packets. Explicitly
 * publish every axis, including zero, and keep each report within signed8 range. */
static void lab_mouse_frame(HvVirtioInput *mouse,int dx, int dy, int wheel_x, int wheel_y, int button, int down) {
    hv_virtio_input_send(mouse, HV_EV_REL, HV_REL_X, dx);
    hv_virtio_input_send(mouse, HV_EV_REL, HV_REL_Y, dy);
    hv_virtio_input_send(mouse, HV_EV_REL, HV_REL_HWHEEL, wheel_x);
    hv_virtio_input_send(mouse, HV_EV_REL, HV_REL_WHEEL, wheel_y);
    if (button>=0) hv_virtio_input_send(mouse, HV_EV_KEY, HV_BTN_LEFT+button, down);
    hv_virtio_input_send(mouse, HV_EV_SYN, 0, 0);
}
static void lab_mouse_move(HvVirtioInput *mouse,int dx,int dy) {
    do {
        int x=dx>100?100:dx< -100?-100:dx;
        int y=dy>100?100:dy< -100?-100:dy;
        lab_mouse_frame(mouse,x,y,0,0,-1,0); dx-=x;dy-=y;
    } while(dx || dy);
}
#endif
