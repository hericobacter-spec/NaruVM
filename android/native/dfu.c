#include <inttypes.h>
#include <stdio.h>
#include "libirecovery.h"
#include "usb_host.h"
#include "dfu.h"

/* The CPU keeps running until this bounded host-side transaction has finished. */
void *lab_dfu_probe(void *opaque) {
    LabDfuProbe *p = opaque;
    p->result = 1;
    int64_t session = hv_usb_wait_online(p->usb, 8000);
    printf("DFU_ONLINE session=%" PRId64 "\n", session);
    if (session < 0) goto done;
    irecv_client_t client = NULL;
    irecv_error_t error = irecv_open_with_ecid(&client, p->ecid);
    if (error != IRECV_E_SUCCESS) {
        printf("DFU_OPEN_FAILED code=%d message=%s\n", error, irecv_strerror(error));
        goto done;
    }
    int mode = 0;
    const struct irecv_device_info *info = irecv_get_device_info(client);
    error = irecv_get_mode(client, &mode);
    printf("DFU_DEVICE mode=0x%04x ecid=0x%016" PRIx64 " cpid=0x%x bdid=0x%x serial=%s\n",
           mode, info ? info->ecid : 0, info ? info->cpid : 0, info ? info->bdid : 0,
           info && info->serial_string ? info->serial_string : "unavailable");
    p->result = error != IRECV_E_SUCCESS || mode != IRECV_K_DFU_MODE ||
                !info || !info->have_ecid || info->ecid != p->ecid;
    irecv_close(client);
done:
    printf("DFU_RESULT failures=%d (virtual guest only)\n", p->result);
    atomic_store(&p->done, 1);
    return NULL;
}
