#define HENGE_SLIRP_LOADER_IMPLEMENTATION
#include "slirp_loader.h"
#include "net_load.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

HvAndroidSlirp hv_android_slirp;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int loaded;

static void load(void) {
    Dl_info own;
    if (!dladdr((void *)&hv_android_slirp_load, &own) || !own.dli_fname) return;
    char directory[4096];
    if (strlen(own.dli_fname) >= sizeof directory) return;
    strcpy(directory, own.dli_fname);
    char *slash = strrchr(directory, '/');
    if (!slash) return;
    *slash = 0;
    void *library = NULL;
    for (size_t i = 0; i < sizeof net_dependencies / sizeof net_dependencies[0]; i++) {
        char path[8192];
        snprintf(path, sizeof path, "%s/%s", directory, net_dependencies[i]);
        library = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (!library) {
            fprintf(stderr, "NETWORK_LOAD_ERROR library=%s error=%s\n", net_dependencies[i], dlerror());
            return;
        }
    }
#define RESOLVE(name) \
    hv_android_slirp.name = (__typeof__(hv_android_slirp.name))dlsym(library, #name); \
    if (!hv_android_slirp.name) { fprintf(stderr, "NETWORK_LOAD_ERROR symbol=%s\n", #name); return; }
    HENGE_SLIRP_FUNCTIONS(RESOLVE)
#undef RESOLVE
    loaded = 1;
}

int hv_android_slirp_load(void) {
    pthread_once(&once, load);
    return loaded ? 0 : -1;
}
