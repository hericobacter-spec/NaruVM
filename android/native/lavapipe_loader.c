/* Offscreen Vulkan ICD adapter for the optional, explicitly selected CPU renderer.
 * Commands delegate to the pinned Mesa ICD. Optional client-path policies are explicit below. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include "lavapipe_load.h"
#include "shader_trace.h"

static pthread_once_t once = PTHREAD_ONCE_INIT;
static PFN_vkGetInstanceProcAddr entry;
static PFN_vkEnumerateDeviceExtensionProperties enumerate_extensions;

/* Diagnostic comparison: let the client select its normal monolithic pipeline
 * implementation without claiming this optional optimization is available. */
static VkResult VKAPI_CALL monolithic_extensions(VkPhysicalDevice physical,
        const char *layer, uint32_t *count, VkExtensionProperties *out) {
    uint32_t total=0;
    VkResult result=enumerate_extensions(physical,layer,&total,NULL);
    if (result != VK_SUCCESS) return result;
    VkExtensionProperties *all=calloc(total ? total : 1,sizeof *all);
    if (!all) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result=enumerate_extensions(physical,layer,&total,all);
    if (result != VK_SUCCESS) { free(all); return result; }
    uint32_t capacity=out ? *count : 0, kept=0;
    for (uint32_t i=0;i<total;++i) {
        if (!strcmp(all[i].extensionName,"VK_EXT_graphics_pipeline_library")) continue;
        if (out && kept<capacity) out[kept]=all[i];
        ++kept;
    }
    free(all);
    *count=out && kept>capacity ? capacity : kept;
    return out && kept>capacity ? VK_INCOMPLETE : VK_SUCCESS;
}

/* Software-runtime workaround: the pinned ICD loses immutable samplers across
 * pushed layout changes (sampler_check.rs). The runtime treats an unavailable
 * optional push command as a request for pooled sets. Raw ICD probes are unchanged. */
static PFN_vkGetDeviceProcAddr policy_device_entry;
static PFN_vkVoidFunction VKAPI_CALL policy_device_proc(VkDevice device, const char *name) {
    if (getenv("HENGE_LVP_POOLED") &&
        (!strcmp(name,"vkCmdPushDescriptorSet") || !strcmp(name,"vkCmdPushDescriptorSetKHR"))) {
        fprintf(stderr,"LAVAPIPE_POOLED optional_command=%s unavailable_for_workaround=1\n",name);
        return NULL;
    }
    return policy_device_entry(device,name);
}

static void load(void) {
    Dl_info own;
    if (!dladdr((void *)&vkGetInstanceProcAddr, &own) || !own.dli_fname) return;
    char directory[4096];
    if (strlen(own.dli_fname) >= sizeof(directory)) return;
    strcpy(directory, own.dli_fname);
    char *slash = strrchr(directory, '/');
    if (!slash) return;
    *slash = 0;
    void *driver = NULL;
    for (size_t i = 0; i < sizeof(dependencies)/sizeof(dependencies[0]); ++i) {
        char path[8192];
        snprintf(path, sizeof(path), "%s/%s", directory, dependencies[i]);
        driver = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (!driver) { fprintf(stderr, "LAVAPIPE_LOAD_ERROR %s: %s\n", dependencies[i], dlerror()); return; }
    }
    PFN_vkNegotiateLoaderICDInterfaceVersion negotiate =
        (PFN_vkNegotiateLoaderICDInterfaceVersion)dlsym(driver, "vk_icdNegotiateLoaderICDInterfaceVersion");
    if (negotiate) {
        uint32_t version = 7;
        VkResult result = negotiate(&version);
        fprintf(stderr, "LAVAPIPE_ICD interface=%u status=%d mode=CPU-offscreen\n", version, result);
        if (result != VK_SUCCESS) return;
    }
    entry = (PFN_vkGetInstanceProcAddr)dlsym(driver, "vk_icdGetInstanceProcAddr");
    if (!entry) fprintf(stderr, "LAVAPIPE_LOAD_ERROR missing vk_icdGetInstanceProcAddr\n");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    pthread_once(&once, load);
    if (entry && instance && getenv("HENGE_LVP_MONOLITHIC") && !strcmp(name,"vkEnumerateDeviceExtensionProperties")) {
        enumerate_extensions=(PFN_vkEnumerateDeviceExtensionProperties)entry(instance,name);
        if (enumerate_extensions) return (PFN_vkVoidFunction)monolithic_extensions;
    }
    if (entry && instance && !strcmp(name,"vkGetDeviceProcAddr") &&
            (getenv("HENGE_SHADER_TRACE") || getenv("HENGE_LVP_POOLED"))) {
        policy_device_entry=(PFN_vkGetDeviceProcAddr)entry(instance,name);
        if (!policy_device_entry) return NULL;
        trace_device_entry=policy_device_proc;
        return getenv("HENGE_SHADER_TRACE") ? (PFN_vkVoidFunction)trace_get_device_proc :
            (PFN_vkVoidFunction)policy_device_proc;
    }
    return entry ? entry(instance, name) : NULL;
}
