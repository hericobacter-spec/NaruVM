#ifndef HENGE_ANDROID_SLIRP_LOADER_H
#define HENGE_ANDROID_SLIRP_LOADER_H
#include <libslirp.h>

/* Types come from the pinned public header; libraries retain their original SONAMEs. */
#define HENGE_SLIRP_FUNCTIONS(X) \
    X(slirp_new) X(slirp_cleanup) X(slirp_input) X(slirp_pollfds_fill_socket) \
    X(slirp_pollfds_poll) X(slirp_handle_timer) X(slirp_add_hostfwd) X(slirp_add_unix)
typedef struct {
#define FIELD(name) __typeof__(&name) name;
    HENGE_SLIRP_FUNCTIONS(FIELD)
#undef FIELD
} HvAndroidSlirp;
extern HvAndroidSlirp hv_android_slirp;
int hv_android_slirp_load(void);
#ifndef HENGE_SLIRP_LOADER_IMPLEMENTATION
#define slirp_new hv_android_slirp.slirp_new
#define slirp_cleanup hv_android_slirp.slirp_cleanup
#define slirp_input hv_android_slirp.slirp_input
#define slirp_pollfds_fill_socket hv_android_slirp.slirp_pollfds_fill_socket
#define slirp_pollfds_poll hv_android_slirp.slirp_pollfds_poll
#define slirp_handle_timer hv_android_slirp.slirp_handle_timer
#define slirp_add_hostfwd hv_android_slirp.slirp_add_hostfwd
#define slirp_add_unix hv_android_slirp.slirp_add_unix
#endif
#endif
