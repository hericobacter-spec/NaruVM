#include <stdio.h>
#include "vm_control.h"

/* Host-only protocol boundary test; opens no VM or disk. */
int main(void) {
    LabControl c;
    const struct { const char *line; int kind, a, b; } valid[] = {
        {"stop", LAB_CONTROL_STOP, 0, 0}, {"release_all", LAB_CONTROL_RELEASE_ALL, 0, 0}, {"hid 4 1", LAB_CONTROL_HID, 4, 1},
        {"hid 231 0", LAB_CONTROL_HID, 231, 0}, {"move -32767 32767", LAB_CONTROL_MOVE, -32767, 32767},
        {"point 0 65535", LAB_CONTROL_POINT, 0, 65535}, {"tap_at 100 200", LAB_CONTROL_TAP_AT, 100, 200},
        {"button 2 0", LAB_CONTROL_BUTTON, 2, 0}, {"scroll -100 100", LAB_CONTROL_SCROLL, -100, 100}
    };
    const char *invalid[] = {"", "stop now", "release_all now", "stop; echo x", "hid 3 1", "hid 256 1", "hid 4 2",
        "hid 4", "hid 4 1 x", "move 32768 0", "move 0 -32768", "move 9999999999999999999999999 0",
        "point -1 20", "tap_at 65536 20", "tap_at 20 2x", "move 1 ", "button -1 0", "button 3 1", "scroll 101 0", "scroll 0 -101", "button 0 1\nstop"};
    int failures = 0;
    for (unsigned i = 0; i < sizeof valid / sizeof *valid; ++i) {
        failures += lab_control_parse(valid[i].line, &c) || c.kind != valid[i].kind || c.a != valid[i].a || c.b != valid[i].b;
    }
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) failures += lab_control_parse(invalid[i], &c) == 0;
    printf("VM_CONTROL_PARSER failures=%d valid=%zu rejected=%zu source=authored no_vm=1\n", failures, sizeof valid/sizeof *valid, sizeof invalid/sizeof *invalid);
    return failures != 0;
}
