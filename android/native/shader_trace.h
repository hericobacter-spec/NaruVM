/* Opt-in, bounded shader evidence. Does not change Vulkan objects or submissions. */
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
static PFN_vkGetDeviceProcAddr trace_device_entry;
static atomic_uint trace_shader_serial;
static atomic_size_t trace_shader_bytes;
static VkResult VKAPI_CALL trace_create_shader(VkDevice device,
        const VkShaderModuleCreateInfo *info, const VkAllocationCallbacks *allocator,
        VkShaderModule *module) {
    PFN_vkCreateShaderModule create=(PFN_vkCreateShaderModule)trace_device_entry(device,"vkCreateShaderModule");
    VkResult result=create(device,info,allocator,module);
    if (result != VK_SUCCESS) return result;
    unsigned serial=atomic_fetch_add(&trace_shader_serial,1);
    size_t bytes=info->codeSize;
    if (bytes>1024*1024 || serial>=4096) return result;
    size_t prior=atomic_fetch_add(&trace_shader_bytes,bytes);
    if (prior>64*1024*1024 || bytes>64*1024*1024-prior) return result;
    if (mkdir("shader-trace",0700) && errno!=EEXIST) return result;
    char path[160];
    snprintf(path,sizeof path,"shader-trace/%ld-%u.spv",(long)getpid(),serial);
    FILE *file=fopen(path,"wb");
    if (!file) return result;
    size_t written=fwrite(info->pCode,1,bytes,file);
    int closed=fclose(file);
    if (written==bytes && !closed)
        fprintf(stderr,"SHADER_TRACE module=%" PRIx64 " bytes=%zu file=%s\n",(uint64_t)(uintptr_t)*module,bytes,path);
    return result;
}
static PFN_vkVoidFunction VKAPI_CALL trace_get_device_proc(VkDevice device,const char *name) {
    if (!strcmp(name,"vkCreateShaderModule")) return (PFN_vkVoidFunction)trace_create_shader;
    return trace_device_entry(device,name);
}
