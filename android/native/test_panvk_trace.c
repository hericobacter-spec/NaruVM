/* Authored ICD stand-in: verifies forwarding, status propagation and ring wrap. */
#include <assert.h>
#include "panvk_loader.c"
static unsigned submitted;
static VKAPI_ATTR VkResult VKAPI_CALL fake_submit(VkQueue q,uint32_t n,const VkSubmitInfo *s,VkFence f) {
    assert(q==(VkQueue)(uintptr_t)7 && n==1 && s && !f);
    submitted++; return submitted==2 || submitted==4?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
}
static PFN_vkVoidFunction VKAPI_CALL fake_proc(VkDevice d,const char *name) {
    assert(d==(VkDevice)(uintptr_t)3);
    return !strcmp(name,"vkQueueSubmit")?(PFN_vkVoidFunction)fake_submit:NULL;
}
static void *writer(void *unused) {
    (void)unused;
    for(unsigned i=0;i<1000;i++) trace_event(TRACE_COUNTER,TRACE_POINT,5,i,0,0);
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2); setenv("HENGE_GPU_TRACE_FILE",argv[1],1);
    device_proc=fake_proc;
    PFN_vkQueueSubmit submit=(PFN_vkQueueSubmit)tracked_device_proc((VkDevice)(uintptr_t)3,"vkQueueSubmit");
    VkSubmitInfo info={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO};
    assert(submit((VkQueue)(uintptr_t)7,1,&info,VK_NULL_HANDLE)==VK_SUCCESS);
    assert(submit((VkQueue)(uintptr_t)7,1,&info,VK_NULL_HANDLE)==VK_ERROR_DEVICE_LOST);
    for(unsigned i=0;i<TRACE_SLOTS+3;i++) trace_event(TRACE_COUNTER,TRACE_POINT,5,i,0,0);
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++) assert(!pthread_create(&threads[i],NULL,writer,NULL));
    for(unsigned i=0;i<4;i++) assert(!pthread_join(threads[i],NULL));
    assert(submit((VkQueue)(uintptr_t)7,1,&info,VK_NULL_HANDLE)==VK_SUCCESS);
    assert(submit((VkQueue)(uintptr_t)7,1,&info,VK_NULL_HANDLE)==VK_ERROR_DEVICE_LOST);
    puts("PANVK_TRACE_TEST forwarding=pass device_lost=pass ring_wrap=pass");
    return 0;
}
