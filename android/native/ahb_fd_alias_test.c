#define _GNU_SOURCE
/* Own 64 KiB AHB only. Public handle transport; no guest/device memory access. */
#include <android/hardware_buffer.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BYTES=65536, PAGE=16384, MAX_FDS=16 };
static uint32_t pattern(unsigned word) { return 0x91a35f20u ^ (word*0x1020305u); }
static int gpu_check(AHardwareBuffer *ahb, uint32_t *alias, uint32_t *scatter) {
    VkInstance instance=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE;
    VkBuffer src=VK_NULL_HANDLE,dst=VK_NULL_HANDLE; VkDeviceMemory smem=VK_NULL_HANDLE,dmem=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
    void *smap=NULL,*dmap=NULL; int result=1,locked=0;
    VkResult vr; const char *stage="instance";
#define TRY(expr) do { stage=#expr; vr=(expr); if (vr!=VK_SUCCESS) { printf("AHB_ALIAS_GPU error=%d stage=%s\n",vr,stage); goto cleanup; } } while (0)
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Henge own AHB alias fixture",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    TRY(vkCreateInstance(&ici,NULL,&instance));
    VkPhysicalDevice physical=VK_NULL_HANDLE,devices[16]; uint32_t count=16;
    TRY(vkEnumeratePhysicalDevices(instance,&count,devices));
    for (uint32_t i=0;i<count;i++) {
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(devices[i],&props);
        if (props.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) { physical=devices[i]; printf("AHB_ALIAS_GPU device=%s vendor=0x%x\n",props.deviceName,props.vendorID); break; }
    }
    if (!physical) { puts("AHB_ALIAS_GPU unavailable=no-hardware-device"); goto cleanup; }
    VkQueueFamilyProperties families[32]; count=32; vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    uint32_t family=UINT32_MAX;
    for (uint32_t i=0;i<count;i++) if (families[i].queueCount && (families[i].queueFlags&VK_QUEUE_TRANSFER_BIT)) { family=i; break; }
    if (family==UINT32_MAX) goto cleanup;
    float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extensions[]={VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    TRY(vkCreateDevice(physical,&di,NULL,&device));
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb=(PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(device,"vkGetAndroidHardwareBufferPropertiesANDROID");
    if (!get_ahb) goto cleanup;
    VkAndroidHardwareBufferPropertiesANDROID ahbp={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    TRY(get_ahb(device,ahb,&ahbp));
    VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=BYTES,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    TRY(vkCreateBuffer(device,&bi,NULL,&src)); bi.pNext=NULL; TRY(vkCreateBuffer(device,&bi,NULL,&dst));
    VkMemoryRequirements sr,dr; vkGetBufferMemoryRequirements(device,src,&sr); vkGetBufferMemoryRequirements(device,dst,&dr);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(physical,&mp);
    uint32_t st=UINT32_MAX,dt=UINT32_MAX;
    for (uint32_t i=0;i<mp.memoryTypeCount;i++) if (mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        if (st==UINT32_MAX && (ahbp.memoryTypeBits&sr.memoryTypeBits&(1u<<i))) st=i;
        if (dt==UINT32_MAX && (dr.memoryTypeBits&(1u<<i))) dt=i;
    }
    if (st==UINT32_MAX || dt==UINT32_MAX) goto cleanup;
    printf("AHB_ALIAS_GPU source_memory_flags=0x%x source_bytes=%" PRIu64 "\n",mp.memoryTypes[st].propertyFlags,(uint64_t)ahbp.allocationSize);
    VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.buffer=src};
    VkImportAndroidHardwareBufferInfoANDROID import={.sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,.pNext=&dedicated,.buffer=ahb};
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,.allocationSize=ahbp.allocationSize,.memoryTypeIndex=st};
    TRY(vkAllocateMemory(device,&ai,NULL,&smem)); ai.pNext=NULL;ai.allocationSize=dr.size;ai.memoryTypeIndex=dt;
    TRY(vkAllocateMemory(device,&ai,NULL,&dmem)); TRY(vkBindBufferMemory(device,src,smem,0));TRY(vkBindBufferMemory(device,dst,dmem,0));
    TRY(vkMapMemory(device,smem,0,VK_WHOLE_SIZE,0,&smap));TRY(vkMapMemory(device,dmem,0,VK_WHOLE_SIZE,0,&dmap));
    VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    TRY(vkCreateCommandPool(device,&pci,NULL,&pool));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command;TRY(vkAllocateCommandBuffers(device,&cai,&command));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};TRY(vkCreateFence(device,&fi,NULL,&fence));
    const unsigned order[4]={0,3,1,2};
    for (unsigned round=1;round<=8;round++) {
        void *native=NULL;
        if (AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&native) || !native) goto cleanup;
        locked=1;
        for (unsigned p=0;p<4;p++) for (unsigned i=0;i<PAGE/4;i++) {
            unsigned src_index=order[p]*(PAGE/4)+i;
            scatter[p*(PAGE/4)+i]=pattern(src_index)^round;
        }
        if (AHardwareBuffer_unlock(ahb,NULL)) goto cleanup; locked=0;
        VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=smem,.offset=0,.size=VK_WHOLE_SIZE};
        TRY(vkFlushMappedMemoryRanges(device,1,&range));
        TRY(vkResetCommandPool(device,pool,0)); TRY(vkResetFences(device,1,&fence));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};TRY(vkBeginCommandBuffer(command,&begin));
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=family,.buffer=src,.offset=0,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
        VkBufferCopy copy={.size=BYTES};vkCmdCopyBuffer(command,src,dst,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
        uint32_t fill=0xa31b0000u|round;vkCmdFillBuffer(command,src,0,BYTES,fill);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.buffer=dst;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);
        barrier.buffer=src;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=0;barrier.srcQueueFamilyIndex=family;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,1,&barrier,0,NULL);
        TRY(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
        TRY(vkQueueSubmit(queue,1,&submit,fence));TRY(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_C(5000000000)));
        range.memory=dmem;TRY(vkInvalidateMappedMemoryRanges(device,1,&range));
        for (unsigned i=0;i<BYTES/4;i++) if (((uint32_t *)dmap)[i]!=(pattern(i)^round)) { printf("AHB_ALIAS_GPU mismatch=alias-to-gpu round=%u word=%u\n",round,i);goto cleanup; }
        if (AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&native) || !native) goto cleanup;
        locked=1;
        for (unsigned i=0;i<BYTES/4;i++) if (alias[i]!=fill || ((uint32_t *)native)[i]!=fill || scatter[i]!=fill) { printf("AHB_ALIAS_GPU mismatch=gpu-to-alias round=%u word=%u\n",round,i);goto cleanup; }
        if (AHardwareBuffer_unlock(ahb,NULL)) goto cleanup;locked=0;
    }
    puts("AHB_ALIAS_GPU rounds=8 words_each=16384 cpu_scatter_to_gpu=pass gpu_write_to_overlap_alias=pass explicit_cache_sync=1 guest_acceleration=unimplemented");result=0;
cleanup:
    if (locked) AHardwareBuffer_unlock(ahb,NULL);
    if (device) {
        vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device,fence,NULL);if (pool) vkDestroyCommandPool(device,pool,NULL);
        if (smap) vkUnmapMemory(device,smem);if (dmap) vkUnmapMemory(device,dmem);
        if (src) vkDestroyBuffer(device,src,NULL);if (dst) vkDestroyBuffer(device,dst,NULL);
        if (smem) vkFreeMemory(device,smem,NULL);if (dmem) vkFreeMemory(device,dmem,NULL);
        vkDestroyDevice(device,NULL);
    }
    if (instance) vkDestroyInstance(instance,NULL);
    return result;
#undef TRY
}
int main(void) {
    alarm(15); setvbuf(stdout,NULL,_IONBF,0);
    AHardwareBuffer *buffer=NULL;
    AHardwareBuffer_Desc desc={.width=BYTES,.height=1,.layers=1,.format=AHARDWAREBUFFER_FORMAT_BLOB,
        .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER};
    int rc=AHardwareBuffer_allocate(&desc,&buffer);
    if (rc || !buffer) { printf("AHB_FD_ALIAS unavailable=allocate rc=%d\n",rc); return 2; }
    void *original=NULL;
    rc=AHardwareBuffer_lock(buffer,desc.usage&0x3f,-1,NULL,&original);
    if (rc || !original) { printf("AHB_FD_ALIAS unavailable=lock rc=%d\n",rc); AHardwareBuffer_release(buffer); return 2; }
    for (unsigned i=0;i<BYTES/4;i++) ((uint32_t *)original)[i]=pattern(i);
    if (AHardwareBuffer_unlock(buffer,NULL)) return 2;
    int sockets[2];
    if (socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,sockets)) return 2;
    struct timeval timeout={.tv_sec=2}; setsockopt(sockets[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    rc=AHardwareBuffer_sendHandleToUnixSocket(buffer,sockets[0]);
    if (rc) { printf("AHB_FD_ALIAS unavailable=send rc=%d\n",rc); return 2; }
    char data[4096]; union { struct cmsghdr alignment; char bytes[CMSG_SPACE(MAX_FDS*sizeof(int))]; } control;
    struct iovec io={.iov_base=data,.iov_len=sizeof data};
    struct msghdr msg={.msg_iov=&io,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof control.bytes};
    ssize_t received=recvmsg(sockets[1],&msg,MSG_CMSG_CLOEXEC);
    int fds[MAX_FDS],count=0,chosen=-1;
    if (received<0 || (msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC))) { puts("AHB_FD_ALIAS unavailable=transport"); return 2; }
    for (struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)) {
        if (c->cmsg_level!=SOL_SOCKET || c->cmsg_type!=SCM_RIGHTS || c->cmsg_len<CMSG_LEN(0)) continue;
        unsigned n=(c->cmsg_len-CMSG_LEN(0))/sizeof(int);
        if (n>MAX_FDS-(unsigned)count) return 2;
        memcpy(fds+count,CMSG_DATA(c),n*sizeof(int)); count+=(int)n;
    }
    close(sockets[0]); close(sockets[1]);
    rc=AHardwareBuffer_lock(buffer,desc.usage&0x3f,-1,NULL,&original);
    if (rc || !original) return 2;
    for (int i=0;i<count;i++) {
        struct stat st;
        if (fstat(fds[i],&st) || st.st_size<BYTES || st.st_size>BYTES+(1<<20)) continue;
        const uint32_t *view=mmap(NULL,BYTES,PROT_READ,MAP_SHARED,fds[i],0);
        if (view==MAP_FAILED) { printf("AHB_FD_MAP index=%d supported=0 errno=%d\n",i,errno); continue; }
        int matches=1;
        for (unsigned j=0;j<BYTES/4;j++) if (view[j]!=pattern(j)) { matches=0; break; }
        munmap((void *)view,BYTES);
        printf("AHB_FD_MAP index=%d bytes=%" PRIu64 " owns_pattern=%d\n",i,(uint64_t)st.st_size,matches);
        if (matches && chosen<0) chosen=i;
    }
    if (AHardwareBuffer_unlock(buffer,NULL)) return 2;
    int failures=0;
    if (chosen<0) { printf("AHB_FD_ALIAS supported=0 fds=%d reason=no-validated-data-fd\n",count); goto end; }
    // Every fixed map below replaces only part of this exact owned reservation.
    uint8_t *reserved=mmap(NULL,3*BYTES,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (reserved==MAP_FAILED) { failures++; goto end; }
    for (int i=0;i<2;i++) if (mmap(reserved+i*BYTES,BYTES,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fds[chosen],0)==MAP_FAILED) {
        printf("AHB_FD_ALIAS supported=0 reason=owned-slot-mmap errno=%d\n",errno); failures++; goto unmap;
    }
    const unsigned order[4]={0,3,1,2};
    for (unsigned i=0;i<4;i++) if (mmap(reserved+2*BYTES+i*PAGE,PAGE,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fds[chosen],order[i]*PAGE)==MAP_FAILED) {
        printf("AHB_FD_ALIAS supported=0 reason=owned-scatter-mmap errno=%d\n",errno); failures++; goto unmap;
    }
    uint32_t *a=(uint32_t *)reserved,*b=(uint32_t *)(reserved+BYTES),*scatter=(uint32_t *)(reserved+2*BYTES);
    for (unsigned round=1;round<=8;round++) {
        rc=AHardwareBuffer_lock(buffer,desc.usage&0x3f,-1,NULL,&original);
        if (rc || !original) { failures++; break; }
        for (unsigned i=0;i<BYTES/4;i++) a[i]=pattern(i)^round;
        for (unsigned i=0;i<BYTES/4;i++) if (b[i]!=(pattern(i)^round) || ((uint32_t *)original)[i]!=(pattern(i)^round)) { failures++; break; }
        for (unsigned page=0;page<4;page++) for (unsigned i=0;i<PAGE/4;i++) {
            unsigned src=order[page]*(PAGE/4)+i;
            if (scatter[page*(PAGE/4)+i]!=(pattern(src)^round)) failures++;
            scatter[page*(PAGE/4)+i]=pattern(src)^round^0x5a5a;
        }
        for (unsigned i=0;i<BYTES/4;i++) if (a[i]!=(pattern(i)^round^0x5a5a) || ((uint32_t *)original)[i]!=(pattern(i)^round^0x5a5a)) { failures++; break; }
        if (AHardwareBuffer_unlock(buffer,NULL)) failures++;
    }
    printf("AHB_FD_ALIAS supported=%d rounds=8 bytes=%d scatter_pages=4 overlap_aliases=2 failures=%d gpu_coherence=untested guest_backend=unimplemented\n",!failures,BYTES,failures);
    if (!failures) failures+=gpu_check(buffer,a,scatter);
unmap:
    munmap(reserved,3*BYTES);
end:
    for (int i=0;i<count;i++) close(fds[i]); AHardwareBuffer_release(buffer);
    return failures ? 1 : chosen<0 ? 2 : 0;
}
