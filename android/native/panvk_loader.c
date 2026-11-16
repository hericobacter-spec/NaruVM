/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit diagnostic loader for the pinned app-private PanVK ICD.
 * No system-driver replacement and no fallback to software or the vendor ICD.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#ifndef HENGE_PANVK_LIBRARY
#define HENGE_PANVK_LIBRARY "libhenge_panvk_driver.so"
#endif
#ifndef HENGE_PANVK_SOURCE
#define HENGE_PANVK_SOURCE "v1.6.0-r10"
#endif

static pthread_once_t once = PTHREAD_ONCE_INIT;
static void *driver;
static PFN_vkGetInstanceProcAddr get_proc;

/* Read-only, process-local kbase diagnostics. Count primary SAME_VA mappings,
 * not their secondary CPU views. Imports can alias physical pages, so this is
 * a region census, not unique system RAM usage. No guest bytes are read. */
union HengeMemQuery {
    struct { uint64_t address, query; } in;
    struct { uint64_t value; } out;
};
#define HENGE_MEM_QUERY _IOWR(0x80, 6, union HengeMemQuery)
__attribute__((visibility("default")))
void henge_panvk_memory_snapshot(void) {
    int fds[16], nfds=0, originals[512], count=0;
    DIR *directory=opendir("/proc/self/fd");
    if(!directory) return;
    struct dirent *entry;
    while((entry=readdir(directory)) && count<512) {
        char *end; long original=strtol(entry->d_name,&end,10);
        if(*end || original<0 || original>INT32_MAX) continue;
        originals[count++]=(int)original;
    }
    closedir(directory);
    for(int i=0;i<count && nfds<16;i++) {
        int copy=fcntl(originals[i],F_DUPFD_CLOEXEC,128);
        if(copy<0) continue;
        char path[64],target[128];
        snprintf(path,sizeof path,"/proc/self/fd/%d",copy);
        ssize_t n=readlink(path,target,sizeof target-1);
        if(n>=0) target[n]=0;
        if(n>=0 && !strcmp(target,"/dev/mali0")) fds[nfds++]=copy;
        else close(copy);
    }
    FILE *maps=fopen("/proc/self/maps","re");
    uint64_t bytes=0,grow_bytes=0,grow_reserved=0,large_bytes=0;
    unsigned queried=0,failed=0,grow_regions=0,large_regions=0,visited=0;
    char line[1024];
    while(maps && fgets(line,sizeof line,maps) && visited++<16384) {
        unsigned long long first,last,offset; char access[5];
        if(!strstr(line,"/dev/mali0") || sscanf(line,"%llx-%llx %4s %llx",&first,&last,access,&offset)!=4 || first!=offset) continue;
        uint64_t committed=0,flags=0; int found=0;
        for(int i=0;i<nfds;i++) {
            union HengeMemQuery query={.in={first,1}};
            if(ioctl(fds[i],HENGE_MEM_QUERY,&query)) continue;
            committed=query.out.value*4096;
            query=(union HengeMemQuery){.in={first,3}};
            if(ioctl(fds[i],HENGE_MEM_QUERY,&query)) continue;
            flags=query.out.value; found=1; break;
        }
        if(!found) { failed++; continue; }
        queried++; bytes+=committed;
        if(flags & (1ull<<9)) {grow_regions++;grow_bytes+=committed;grow_reserved+=last-first;}
        if(last-first>=(16ull<<20)) {large_regions++;large_bytes+=committed;}
    }
    if(maps) fclose(maps);
    for(int i=0;i<nfds;i++) close(fds[i]);
    fprintf(stderr,"PANVK_KBASE_CENSUS contexts=%d regions=%u query_failed=%u committed_bytes=%" PRIu64 " grow_regions=%u grow_committed_bytes=%" PRIu64 " grow_reserved_bytes=%" PRIu64 " large_regions=%u large_committed_bytes=%" PRIu64 " unique_physical=0\n",nfds,queried,failed,bytes,grow_regions,grow_bytes,grow_reserved,large_regions,large_bytes);
}
static void sample_memory(void) {
    static atomic_long last;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    long previous=atomic_load(&last);
    if(now.tv_sec-previous<30 || !atomic_compare_exchange_strong(&last,&previous,now.tv_sec)) return;
    henge_panvk_memory_snapshot();
}

static void load_driver(void) {
    driver = dlopen(HENGE_PANVK_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (!driver) {
        fprintf(stderr, "PANVK_LOAD_ERROR %s\n", dlerror());
        return;
    }
    PFN_vkNegotiateLoaderICDInterfaceVersion negotiate =
        (PFN_vkNegotiateLoaderICDInterfaceVersion)dlsym(driver, "vk_icdNegotiateLoaderICDInterfaceVersion");
    if (negotiate) {
        uint32_t version = 7;
        VkResult result = negotiate(&version);
        fprintf(stderr, "PANVK_ICD interface=%u status=%d\n", version, result);
        if (result != VK_SUCCESS) return;
    }
    get_proc = (PFN_vkGetInstanceProcAddr)dlsym(driver, "vk_icdGetInstanceProcAddr");
    if (!get_proc) fprintf(stderr, "PANVK_LOAD_ERROR missing vk_icdGetInstanceProcAddr\n");
    else fprintf(stderr, "PANVK_LOADER source=%s private_icd=1 fallback=none\n", HENGE_PANVK_SOURCE);
    /* Keep the ICD loaded for process lifetime, including outstanding objects. */
}

/* Opt-in allocation accounting: imported guest pages are separate from native
 * Vulkan allocations. This cannot account for the driver's private allocations. */
static _Atomic(PFN_vkGetDeviceProcAddr) device_proc;
#include "panvk_trace.h"
static pthread_mutex_t memory_lock=PTHREAD_MUTEX_INITIALIZER;
typedef struct MemoryRow { VkDevice device; VkDeviceMemory memory; uint64_t bytes; int imported; struct MemoryRow *next; } MemoryRow;
static MemoryRow *memory_rows;
static uint64_t live_bytes, import_bytes, peak_bytes, memory_count, reported_peak;
static VKAPI_ATTR VkResult VKAPI_CALL tracked_allocate(VkDevice d,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *a,VkDeviceMemory *out) {
    PFN_vkAllocateMemory fn=(PFN_vkAllocateMemory)device_proc(d,"vkAllocateMemory");
    VkResult result=fn(d,info,a,out);
    trace_event(TRACE_ALLOC,TRACE_POINT,result==VK_SUCCESS?TRACE_HANDLE(*out):0,info->allocationSize,info->memoryTypeIndex,result);
    if(result!=VK_SUCCESS) return result;
    MemoryRow *row=malloc(sizeof *row); if(!row) return result;
    int imported=0;
    for(const VkBaseInStructure *p=info->pNext;p;p=p->pNext)
        if(p->sType==VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT ||
           p->sType==VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR) imported=1;
    if(imported) trace_event(TRACE_ALLOC,TRACE_POINT,TRACE_HANDLE(*out),info->allocationSize,1,0);
    pthread_mutex_lock(&memory_lock);
    *row=(MemoryRow){d,*out,info->allocationSize,imported,memory_rows}; memory_rows=row;
    if(imported) import_bytes+=row->bytes; else live_bytes+=row->bytes;
    memory_count++;
    int report=live_bytes>=reported_peak+(64ull<<20) || row->bytes>=(64ull<<20);
    if(live_bytes>peak_bytes) peak_bytes=live_bytes;
    if(report) reported_peak=peak_bytes;
    if(report) fprintf(stderr,"PANVK_MEMORY native_bytes=%" PRIu64 " imported_bytes=%" PRIu64 " peak_native_bytes=%" PRIu64 " allocations=%" PRIu64 " added_bytes=%" PRIu64 " imported=%d\n",live_bytes,import_bytes,peak_bytes,memory_count,row->bytes,imported);
    pthread_mutex_unlock(&memory_lock);
    sample_memory();
    return result;
}
static VKAPI_ATTR void VKAPI_CALL tracked_free(VkDevice d,VkDeviceMemory memory,const VkAllocationCallbacks *a) {
    uint64_t id=trace_event(TRACE_FREE,TRACE_BEGIN,TRACE_HANDLE(memory),0,0,0);
    pthread_mutex_lock(&memory_lock);
    MemoryRow **at=&memory_rows;
    while(*at && ((*at)->device!=d || (*at)->memory!=memory)) at=&(*at)->next;
    if(*at) { MemoryRow *row=*at; *at=row->next;
        if(row->imported) import_bytes-=row->bytes; else live_bytes-=row->bytes;
        memory_count--;free(row);
    }
    pthread_mutex_unlock(&memory_lock);
    ((PFN_vkFreeMemory)device_proc(d,"vkFreeMemory"))(d,memory,a);
    trace_event(TRACE_FREE,TRACE_END,TRACE_HANDLE(memory),id,0,0);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL tracked_device_proc(VkDevice d,const char *name) {
    PFN_vkVoidFunction fn=device_proc(d,name);
    if(!fn) return NULL;
    if(!strcmp(name,"vkAllocateMemory")) return (PFN_vkVoidFunction)tracked_allocate;
    if(!strcmp(name,"vkFreeMemory")) return (PFN_vkVoidFunction)tracked_free;
    if(getenv("HENGE_GPU_TRACE_FILE")) {
        if(!strcmp(name,"vkQueueSubmit")) { real_submit=(PFN_vkQueueSubmit)fn; return (PFN_vkVoidFunction)trace_submit; }
        if(!strcmp(name,"vkQueueWaitIdle")) { real_queue_idle=(PFN_vkQueueWaitIdle)fn; return (PFN_vkVoidFunction)trace_queue_idle; }
        if(!strcmp(name,"vkDeviceWaitIdle")) return (PFN_vkVoidFunction)trace_device_idle;
        if(!strcmp(name,"vkWaitSemaphores")) return (PFN_vkVoidFunction)trace_wait;
        if(!strcmp(name,"vkGetSemaphoreCounterValue")) return (PFN_vkVoidFunction)trace_counter;
        if(!strcmp(name,"vkWaitForFences")) return (PFN_vkVoidFunction)trace_fences;
        if(!strcmp(name,"vkCreateGraphicsPipelines")) return (PFN_vkVoidFunction)trace_graphics;
        if(!strcmp(name,"vkCreateComputePipelines")) return (PFN_vkVoidFunction)trace_compute;
    }
    return fn;
}

__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    pthread_once(&once, load_driver);
    PFN_vkVoidFunction fn=get_proc ? get_proc(instance,name) : NULL;
    if(fn && (getenv("HENGE_GPU_MEMORY_TRACE") || getenv("HENGE_GPU_TRACE_FILE")) && !strcmp(name,"vkGetDeviceProcAddr")) {
        device_proc=(PFN_vkGetDeviceProcAddr)fn;
        return (PFN_vkVoidFunction)tracked_device_proc;
    }
    return fn;
}
