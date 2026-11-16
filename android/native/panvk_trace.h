/* Process-local, opt-in circular trace. Opaque Vulkan handles only: no resource
 * contents or imported host-memory addresses. API timing includes tracing. */
#include <sys/stat.h>

enum { TRACE_ALLOC=1, TRACE_FREE, TRACE_SUBMIT, TRACE_WAIT, TRACE_IDLE,
       TRACE_PIPELINE, TRACE_UNMAP, TRACE_SIGNAL, TRACE_COUNTER };
enum { TRACE_BEGIN=1, TRACE_END=2, TRACE_POINT=3 };
#define TRACE_SLOTS 8192
typedef struct {
    uint64_t sequence, ns, thread, kind, phase, object, a, b;
    int64_t result;
    uint64_t committed;
} TraceEvent;
static pthread_once_t trace_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t trace_lock=PTHREAD_MUTEX_INITIALIZER;
static int trace_fd=-1;
static uint64_t trace_sequence;
static uint64_t trace_clock(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000000000ull+now.tv_nsec;
}
static void trace_open(void) {
    const char *path=getenv("HENGE_GPU_TRACE_FILE");
    if (!path || !*path) return;
    trace_fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if (trace_fd<0) { fprintf(stderr,"PANVK_TRACE_ERROR open_failed=1\n"); return; }
    const uint64_t header[4]={0x3145434152545048ull,sizeof(TraceEvent),TRACE_SLOTS,(uint64_t)getpid()};
    if (ftruncate(trace_fd,32+TRACE_SLOTS*sizeof(TraceEvent)) ||
        pwrite(trace_fd,header,sizeof header,0)!=(ssize_t)sizeof header) {
        close(trace_fd); trace_fd=-1;
        fprintf(stderr,"PANVK_TRACE_ERROR initialize_failed=1\n"); return;
    }
    fprintf(stderr,"PANVK_TRACE active=1 slots=%u bytes=%zu version=1\n",TRACE_SLOTS,32+TRACE_SLOTS*sizeof(TraceEvent));
}
static uint64_t trace_event(unsigned kind,unsigned phase,uint64_t object,uint64_t a,uint64_t b,int64_t result) {
    pthread_once(&trace_once,trace_open);
    pthread_mutex_lock(&trace_lock);
    if (trace_fd<0) { pthread_mutex_unlock(&trace_lock); return 0; }
    uint64_t sequence=++trace_sequence;
    TraceEvent event={sequence,trace_clock(),(uintptr_t)pthread_self(),kind,phase,object,a,b,result,sequence};
    off_t offset=32+((sequence-1)%TRACE_SLOTS)*sizeof event;
    if (pwrite(trace_fd,&event,sizeof event,offset)!=(ssize_t)sizeof event) {
        close(trace_fd); trace_fd=-1;
        fprintf(stderr,"PANVK_TRACE_ERROR write_failed=1\n");
    }
    pthread_mutex_unlock(&trace_lock);
    return sequence;
}
#define TRACE_HANDLE(h) ((uint64_t)(uintptr_t)(h))
static _Atomic(PFN_vkQueueSubmit) real_submit;
static _Atomic(PFN_vkQueueWaitIdle) real_queue_idle;

static VKAPI_ATTR VkResult VKAPI_CALL trace_submit(VkQueue q,uint32_t n,const VkSubmitInfo *infos,VkFence fence) {
    uint64_t commands=0;
    for(uint32_t i=0;i<n;i++) commands+=infos[i].commandBufferCount;
    uint64_t id=trace_event(TRACE_SUBMIT,TRACE_BEGIN,TRACE_HANDLE(q),n,commands,TRACE_HANDLE(fence));
    for(uint32_t i=0;i<n;i++) {
        const VkTimelineSemaphoreSubmitInfo *timeline=NULL;
        for(const VkBaseInStructure *p=infos[i].pNext;p;p=p->pNext)
            if(p->sType==VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) timeline=(const void *)p;
        for(uint32_t j=0;j<infos[i].signalSemaphoreCount;j++)
            trace_event(TRACE_SIGNAL,TRACE_POINT,TRACE_HANDLE(infos[i].pSignalSemaphores[j]),
                timeline && j<timeline->signalSemaphoreValueCount?timeline->pSignalSemaphoreValues[j]:0,id,1);
    }
    VkResult result=real_submit(q,n,infos,fence);
    trace_event(TRACE_SUBMIT,TRACE_END,TRACE_HANDLE(q),id,0,result);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_wait(VkDevice d,const VkSemaphoreWaitInfo *info,uint64_t timeout) {
    uint64_t id=trace_event(TRACE_WAIT,TRACE_BEGIN,TRACE_HANDLE(d),info->semaphoreCount,timeout,info->flags);
    for(uint32_t i=0;i<info->semaphoreCount;i++)
        trace_event(TRACE_SIGNAL,TRACE_POINT,TRACE_HANDLE(info->pSemaphores[i]),info->pValues[i],id,0);
    VkResult result=((PFN_vkWaitSemaphores)device_proc(d,"vkWaitSemaphores"))(d,info,timeout);
    trace_event(TRACE_WAIT,TRACE_END,TRACE_HANDLE(d),id,0,result);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_counter(VkDevice d,VkSemaphore semaphore,uint64_t *value) {
    VkResult result=((PFN_vkGetSemaphoreCounterValue)device_proc(d,"vkGetSemaphoreCounterValue"))(d,semaphore,value);
    trace_event(TRACE_COUNTER,TRACE_POINT,TRACE_HANDLE(semaphore),result==VK_SUCCESS?*value:0,0,result);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_queue_idle(VkQueue q) {
    uint64_t id=trace_event(TRACE_IDLE,TRACE_BEGIN,TRACE_HANDLE(q),0,0,0);
    VkResult result=real_queue_idle(q);
    trace_event(TRACE_IDLE,TRACE_END,TRACE_HANDLE(q),id,0,result); return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_device_idle(VkDevice d) {
    uint64_t id=trace_event(TRACE_IDLE,TRACE_BEGIN,TRACE_HANDLE(d),1,0,0);
    VkResult result=((PFN_vkDeviceWaitIdle)device_proc(d,"vkDeviceWaitIdle"))(d);
    trace_event(TRACE_IDLE,TRACE_END,TRACE_HANDLE(d),id,0,result); return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_fences(VkDevice d,uint32_t n,const VkFence *f,VkBool32 all,uint64_t timeout) {
    uint64_t id=trace_event(TRACE_WAIT,TRACE_BEGIN,TRACE_HANDLE(d),n,timeout,all);
    VkResult result=((PFN_vkWaitForFences)device_proc(d,"vkWaitForFences"))(d,n,f,all,timeout);
    trace_event(TRACE_WAIT,TRACE_END,TRACE_HANDLE(d),id,0,result); return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_graphics(VkDevice d,VkPipelineCache cache,uint32_t n,const VkGraphicsPipelineCreateInfo *infos,const VkAllocationCallbacks *a,VkPipeline *out) {
    uint64_t id=trace_event(TRACE_PIPELINE,TRACE_BEGIN,TRACE_HANDLE(d),n,0,0);
    VkResult result=((PFN_vkCreateGraphicsPipelines)device_proc(d,"vkCreateGraphicsPipelines"))(d,cache,n,infos,a,out);
    trace_event(TRACE_PIPELINE,TRACE_END,TRACE_HANDLE(d),id,0,result); return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trace_compute(VkDevice d,VkPipelineCache cache,uint32_t n,const VkComputePipelineCreateInfo *infos,const VkAllocationCallbacks *a,VkPipeline *out) {
    uint64_t id=trace_event(TRACE_PIPELINE,TRACE_BEGIN,TRACE_HANDLE(d),n,1,0);
    VkResult result=((PFN_vkCreateComputePipelines)device_proc(d,"vkCreateComputePipelines"))(d,cache,n,infos,a,out);
    trace_event(TRACE_PIPELINE,TRACE_END,TRACE_HANDLE(d),id,0,result); return result;
}
