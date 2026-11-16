#include "installed_guard.h"
#include <assert.h>
static void write_file(const char *dir,const char *name,const char *text) {
    char path[4096];assert(clone_guard_path(path,sizeof path,dir,name));
    FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));
}
int main(void) {
    char dir[]="/tmp/naru-guard-XXXXXX",path[4096];assert(mkdtemp(dir));
    uint64_t identity=17;
    assert(!naru_machine_ecid(dir,&identity)&&identity==17);
    write_file(dir,"identity.txt","12345678912345\n");assert(!naru_machine_ecid(dir,&identity)&&identity==12345678912345ull);
    write_file(dir,"identity.txt","-12345678912345\n");assert(naru_machine_ecid(dir,&identity));
    write_file(dir,"identity.txt","184467440737095516150\n");assert(naru_machine_ecid(dir,&identity));
    assert(naru_boot_guard(dir));
    write_file(dir,"installed.txt","NARUVM_INSTALLED_V1\nbuild=26B123\n");assert(naru_boot_guard(dir));
    const char *names[]={"root.raw","auxiliary.bin"};off_t sizes[]={64ull<<30,33570816};
    for(unsigned i=0;i<2;i++) {
        assert(clone_guard_path(path,sizeof path,dir,names[i]));int fd=open(path,O_CREAT|O_RDWR,0600);assert(fd>=0);assert(!ftruncate(fd,sizes[i]));close(fd);
    }
    assert(!naru_boot_guard(dir));
    write_file(dir,"installed.txt","NARUVM_INSTALLED_V1\nbuild=unknown\n");assert(naru_boot_guard(dir));
    write_file(dir,"installed.txt","NARUVM_INSTALLED_V1\nbuild=24G90\nextra");assert(naru_boot_guard(dir));
    write_file(dir,"installed.txt","NARUVM_INSTALLED_V1\nbuild=25G83\n");assert(!naru_boot_guard(dir));
    assert(clone_guard_path(path,sizeof path,dir,"root.raw"));assert(!unlink(path));assert(!symlink("/dev/null",path));assert(naru_boot_guard(dir));
    for(unsigned i=0;i<2;i++){assert(clone_guard_path(path,sizeof path,dir,names[i]));assert(!unlink(path));}
    assert(clone_guard_path(path,sizeof path,dir,"identity.txt"));assert(!unlink(path));
    assert(clone_guard_path(path,sizeof path,dir,"installed.txt"));assert(!unlink(path));assert(!rmdir(dir));
    printf("INSTALLED_GUARD_RESULT scenarios=11 failures=0 guest=0\n");return 0;
}
