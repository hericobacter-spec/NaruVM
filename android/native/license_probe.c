/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Disk-free recipient-library probe; no Apple material or network access. */
#include <plist/plist.h>
#include <stdio.h>
#include <stdlib.h>

int lab_license_probe(void) {
    plist_t value = plist_new_string("NaruVM LGPL replacement probe");
    if (!value) return 1;
    char *text = NULL;
    plist_get_string_val(value, &text);
    int failed = !text || !*text;
    if (!failed) printf("LGPL_REPLACEMENT_VALUE %s\n", text);
    free(text);
    plist_free(value);
    return failed;
}
