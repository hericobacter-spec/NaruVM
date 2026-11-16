/* SPDX-License-Identifier: GPL-2.0-or-later
 * App-private optional Turnip ICD adapter. Never changes the system driver.
 * Build.py stages an explicitly supplied, hash-checked driver with provenance.
 * Successful loading alone is not eligibility: the Reims pixel probe is required. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
static pthread_once_t once=PTHREAD_ONCE_INIT;
static PFN_vkGetInstanceProcAddr entry;
static void load(void) {
    void *driver=dlopen("libhenge_turnip_driver.so",RTLD_NOW|RTLD_LOCAL);
    if(!driver) { fprintf(stderr,"TURNIP_LOAD_ERROR %s\n",dlerror()); return; }
    PFN_vkNegotiateLoaderICDInterfaceVersion negotiate=(PFN_vkNegotiateLoaderICDInterfaceVersion)dlsym(driver,"vk_icdNegotiateLoaderICDInterfaceVersion");
    uint32_t version=7;
    if(negotiate && negotiate(&version)!=VK_SUCCESS) return;
    entry=(PFN_vkGetInstanceProcAddr)dlsym(driver,"vk_icdGetInstanceProcAddr");
    fprintf(stderr,"TURNIP_LOADER private_icd=1 fallback=none entry=%d\n",entry!=NULL);
    /* Driver remains loaded for process lifetime. */
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {
    pthread_once(&once,load);return entry?entry(instance,name):NULL;
}
