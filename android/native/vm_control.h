#ifndef HENGE_LAB_VM_CONTROL_H
#define HENGE_LAB_VM_CONTROL_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { LAB_CONTROL_STOP, LAB_CONTROL_HID, LAB_CONTROL_MOVE, LAB_CONTROL_BUTTON, LAB_CONTROL_SCROLL, LAB_CONTROL_POINT, LAB_CONTROL_TAP_AT };
typedef struct { int kind, a, b; } LabControl;

/* A bounded line protocol on the worker's private stdin; never a shell command. */
static int lab_control_parse(const char *line, LabControl *out) {
    if (!strcmp(line, "stop")) { *out = (LabControl){LAB_CONTROL_STOP, 0, 0}; return 0; }
    const struct { const char *name; int kind, low, high, second_low, second_high; } commands[] = {
        {"hid ", LAB_CONTROL_HID, 4, 255, 0, 1},
        {"move ", LAB_CONTROL_MOVE, -32767, 32767, -32767, 32767},
        {"button ", LAB_CONTROL_BUTTON, 0, 2, 0, 1},
        {"scroll ", LAB_CONTROL_SCROLL, -100, 100, -100, 100},
        {"point ", LAB_CONTROL_POINT, 0, 65535, 0, 65535},
        {"tap_at ", LAB_CONTROL_TAP_AT, 0, 65535, 0, 65535}
    };
    for (unsigned i = 0; i < sizeof commands / sizeof *commands; ++i) {
        size_t n = strlen(commands[i].name);
        if (strncmp(line, commands[i].name, n)) continue;
        char *end;
        errno = 0; long a = strtol(line + n, &end, 10);
        if (errno || end == line + n || *end != ' ' || a < commands[i].low || a > commands[i].high) return -1;
        const char *second = end + 1;
        errno = 0; long b = strtol(second, &end, 10);
        if (errno || end == second || *end || b < commands[i].second_low || b > commands[i].second_high) return -1;
        *out = (LabControl){commands[i].kind, (int)a, (int)b}; return 0;
    }
    return -1;
}
#endif
