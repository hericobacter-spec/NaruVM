#ifndef NARU_INSTALLED_GUARD_H
#define NARU_INSTALLED_GUARD_H
#include "disk_clone_guard.h"
#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
/* The legacy protected-clone contract stays intact. Fresh managed installs have
 * their own completion marker, created only after a successful, flushed restore. */
static int naru_installed_guard(const char *dir) {
    char path[4096], marker[96];struct stat st;
    if(lstat(dir,&st)||!S_ISDIR(st.st_mode)||!clone_guard_path(path,sizeof path,dir,"installed.txt"))return -1;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return -1;
    ssize_t n=read(fd,marker,sizeof marker-1);int regular=!fstat(fd,&st)&&S_ISREG(st.st_mode);close(fd);
    if(!regular||n<=0)return -1;
    marker[n]=0;
    if(strcmp(marker,"NARUVM_INSTALLED_V1\nbuild=24G90\n")&&
       strcmp(marker,"NARUVM_INSTALLED_V1\nbuild=25G83\n")&&
       strcmp(marker,"NARUVM_INSTALLED_V1\nbuild=26B123\n"))return -1;
    const char *names[]={"root.raw","auxiliary.bin"};
    const uint64_t sizes[]={64ull<<30,33570816};
    for(unsigned i=0;i<2;i++)
        if(!clone_guard_path(path,sizeof path,dir,names[i])||lstat(path,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=sizes[i])return -1;
    return 0;
}
static int naru_boot_guard(const char *dir) {
    if(!naru_installed_guard(dir)){printf("NARU_INSTALLED_GUARD complete=1\n");return 0;}
    return hv_research_clone_guard(dir);
}
static int naru_machine_ecid(const char *dir,uint64_t *ecid) {
    char path[4096],text[40];struct stat st;
    if(!clone_guard_path(path,sizeof path,dir,"identity.txt"))return -1;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return errno==ENOENT?0:-1; /* legacy machine keeps its original identity */
    ssize_t n=read(fd,text,sizeof text-1);int regular=!fstat(fd,&st)&&S_ISREG(st.st_mode);close(fd);
    if(!regular||n<2||n>20||text[n-1]!='\n')return -1;
    text[n-1]=0;for(ssize_t i=0;i<n-1;i++)if(text[i]<'0'||text[i]>'9')return -1;
    char *end;errno=0;unsigned long long value=strtoull(text,&end,10);
    if(errno||*end||value<(1ull<<40)||value>INT64_MAX)return -1;
    *ecid=value;return 0;
}
#endif
