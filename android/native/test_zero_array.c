#define HV_TEST_ZERO_ARRAY 1
#include "../../host/zero_array.h"
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>

static void fault_check(unsigned char *at,size_t bytes,int tail) {
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        freopen("/dev/null","w",stderr);
        signal(SIGSEGV,SIG_DFL);signal(SIGBUS,SIG_DFL);signal(SIGABRT,SIG_DFL);
        if (tail) { at[bytes]^=1;hv_zero_array_free(at,bytes,1); }
        else at[-1]=1;
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status));
    assert(tail ? WTERMSIG(status)==SIGABRT : (WTERMSIG(status)==SIGSEGV || WTERMSIG(status)==SIGBUS));
}
int main(void) {
    size_t lengths[]={17,1u<<20,(1u<<20)+37,64u<<20};
    for (unsigned k=0;k<sizeof lengths/sizeof lengths[0];k++) {
        size_t n=lengths[k];unsigned char *at=hv_zero_array(n,1);assert(at);
        for (size_t i=0;i<n;i++) assert(at[i]==0);
        for (size_t i=0;i<n;i+=4096) at[i]=(unsigned char)(i/4096);
        at[n-1]=91;assert(at[n-1]==91);
        if (n>=(1u<<20)) {fault_check(at,n,0);fault_check(at,n,1);}
        hv_zero_array_free(at,n,1);
    }
    assert(!hv_zero_array(SIZE_MAX,2));
    hv_zero_array_free(NULL,0,0);
    puts("ZERO_ARRAY_TEST zero_initialization=pass sparse_writes=pass prefix_guard=pass tail_guard=pass overflow=pass");
}
