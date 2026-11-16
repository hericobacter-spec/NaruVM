/* Extension enumeration contract for the optional diagnostic policy. */
#include "lavapipe_loader.c"
#include <assert.h>
static VkResult VKAPI_CALL fake_extensions(VkPhysicalDevice p,const char *l,uint32_t *count,VkExtensionProperties *out) {
    const char *names[]={"VK_TEST_first","VK_EXT_graphics_pipeline_library","VK_TEST_last"};
    if (!out) { *count=3; return VK_SUCCESS; }
    uint32_t n=*count<3?*count:3;
    for(uint32_t i=0;i<n;i++) { memset(out+i,0,sizeof *out); strcpy(out[i].extensionName,names[i]);out[i].specVersion=i+1; }
    *count=n; return n<3?VK_INCOMPLETE:VK_SUCCESS;
}
static void VKAPI_CALL sentinel(void) {}
static PFN_vkVoidFunction VKAPI_CALL fake_device(VkDevice device,const char *name) { return sentinel; }
int main(void) {
    policy_device_entry=fake_device;
    unsetenv("HENGE_LVP_POOLED");
    assert(policy_device_proc(0,"vkCmdPushDescriptorSet")==sentinel);
    setenv("HENGE_LVP_POOLED","1",1);
    assert(!policy_device_proc(0,"vkCmdPushDescriptorSet"));
    assert(!policy_device_proc(0,"vkCmdPushDescriptorSetKHR"));
    assert(policy_device_proc(0,"vkQueueSubmit")==sentinel);
    unsetenv("HENGE_LVP_POOLED");

    enumerate_extensions=fake_extensions;
    uint32_t n=0; assert(monolithic_extensions(0,NULL,&n,NULL)==VK_SUCCESS && n==2);
    VkExtensionProperties p[4]; memset(p,0xa5,sizeof p);
    n=1; assert(monolithic_extensions(0,NULL,&n,p)==VK_INCOMPLETE && n==1);
    assert(!strcmp(p[0].extensionName,"VK_TEST_first") && p[0].specVersion==1);
    n=4; assert(monolithic_extensions(0,NULL,&n,p)==VK_SUCCESS && n==2);
    assert(!strcmp(p[1].extensionName,"VK_TEST_last") && p[1].specVersion==3);
    n=0; assert(monolithic_extensions(0,NULL,&n,p)==VK_INCOMPLETE && n==0);
    puts("LAVAPIPE_POLICY_TEST count partial full zero_capacity preserved_fields=pass"); return 0;
}
