#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>
extern void native_factory_call(void*,void*,const char*,int,void*);
__asm__(".text\n.global native_factory_call\n.type native_factory_call,%function\nnative_factory_call:\nmov x8,x0\nmov x0,x1\nmov x1,x2\nmov x2,x3\nbr x4\n");
static uintptr_t engine_base;
static void crash(int sig,siginfo_t* info,void* raw) {
    ucontext_t* uc=raw;
    dprintf(2,"PROBE_CRASH sig=%d addr=%p pc=%#lx lr=%#lx base=%#lx\n",sig,info->si_addr,
        (unsigned long)uc->uc_mcontext.pc,(unsigned long)uc->uc_mcontext.regs[30],(unsigned long)engine_base);
    for(int i=0;i<8;i++)dprintf(2,"x%d=%#lx ",i,(unsigned long)uc->uc_mcontext.regs[i]);
    dprintf(2,"\n");
    void* frames[24];int count=backtrace(frames,24);
    for(int i=0;i<count;i++){Dl_info d={0};dladdr(frames[i],&d);dprintf(2,"FRAME %s +%#lx\n",d.dli_fname?d.dli_fname:"?",(unsigned long)((uintptr_t)frames[i]-(uintptr_t)d.dli_fbase));}
    _exit(90);
}
static int locate(struct dl_phdr_info* info, size_t size, void* opaque) {
    (void)size; (void)opaque;
    const char* name=info->dlpi_name;
    if (name && (!*name || strstr(name,"/usr/Engine/Engine"))) {
        engine_base=info->dlpi_addr;
        return 1;
    }
    return 0;
}
/* Research harness: invoke native file/reader routines without application main.
 * Firmware-specific offsets are guarded by the Python runner's SHA256 check.
 * The process exits without teardown; this is not a production adapter. */
int __libc_start_main(int (*main_fn)(int,char**,char**),int argc,char** argv,
        void (*init)(void),void (*fini)(void),void (*rtld_fini)(void),void* stack) {
    (void)main_fn;(void)argc;(void)argv;(void)init;(void)fini;(void)rtld_fini;(void)stack;
    dl_iterate_phdr(locate,0);
    if (!engine_base) {dprintf(2,"ENTRY_PROBE missing Engine mapping\n");_exit(2);}
    const char* type=(const char*)(engine_base+0x25a4ea0);
    uintptr_t reader_address=*(uintptr_t*)(engine_base+0x2ae1c70);
    int good=!strcmp(type,"16ffmpegFileReader") && reader_address==engine_base+0x1612420;
    dprintf(1,"ENTRY_PROBE base=%#lx type=%s read_wrapper=%#lx expected=%#lx\n",
            (unsigned long)engine_base,type,(unsigned long)reader_address,
            (unsigned long)(engine_base+0x1612420));
    dprintf(1,"ENTRY_PROBE %s; Engine main not invoked; beginning explicit reader probe\n",good?"PASS":"FAIL");
    if (!good) _exit(3);
    struct sigaction sa={0};sa.sa_sigaction=crash;sa.sa_flags=SA_SIGINFO;
    sigaction(SIGSEGV,&sa,0);sigaction(SIGABRT,&sa,0);sigaction(SIGBUS,&sa,0);
    if (getenv("ENGINE_READER_PREROLL_NINE")) {
        /* Opt-in research only. This common-path change is not MP3-scoped.
         * Keep the on-disk executable immutable and fail on unexpected code. */
        uint32_t* instruction=(uint32_t*)(engine_base+0x1611c50);
        if (*instruction!=0x12800043u) _exit(20);
        long pagesize=sysconf(_SC_PAGESIZE);
        if (pagesize<=0) _exit(21);
        uintptr_t page=(uintptr_t)instruction & ~((uintptr_t)pagesize-1);
        if (mprotect((void*)page,(size_t)pagesize,PROT_READ|PROT_WRITE|PROT_EXEC)) _exit(22);
        *instruction=0x12800103u;
        __builtin___clear_cache((char*)instruction,(char*)(instruction+1));
        if (mprotect((void*)page,(size_t)pagesize,PROT_READ|PROT_EXEC)) _exit(23);
        dprintf(1,"EXPERIMENT preroll=9; common reader; process memory only\n");
    }
    const char* fresh=getenv("ENGINE_READER_FRESH_SEEK");
    int64_t fresh_position=0;
    if (fresh) {
        char* end=0;errno=0;
        fresh_position=strtoll(fresh,&end,10);
        if (errno || end==fresh || *end || fresh_position<0 || fresh_position>INT64_MAX-256) _exit(24);
    }
    const char* input=getenv("ENGINE_READER_INPUT");
    if (!input || !*input) _exit(4);
    void* (*allocate)(size_t)=(void*(*)(size_t))(engine_base+0x3915d8);
    void (*reader_ctor)(void*,void**)=(void(*)(void*,void**))(engine_base+0x1619610);
    void (*factory_ctor)(void*,int)=(void(*)(void*,int))(engine_base+0x17410f0);
    void* factory=allocate(0x88);
    factory_ctor(factory,0);
    void* file=0;
    dprintf(1,"READER_PROBE creating file through native airFileFactory\n");
    native_factory_call(&file,factory,input,0,(void*)(engine_base+0x174f820));
    if(!file){dprintf(2,"READER_PROBE file factory returned null\n");_exit(6);}
    dprintf(1,"READER_PROBE file constructed vtable=%#lx\n",(unsigned long)(*(uintptr_t*)file-engine_base));
    void* reader=allocate(0x150);
    dprintf(1,"READER_PROBE constructing native ffmpegFileReader\n");
    reader_ctor(reader,&file);
    unsigned char* fields=reader;
    int open=fields[0x34]&1;
    dprintf(1,"READER_PROBE open=%d channels=%d skip=%d packet_delay=%lld origin=%lld consumed_file=%d\n",
      open,*(int*)(fields+0x88),*(int*)(fields+0xd8),
      (long long)*(int64_t*)(fields+0xc8),(long long)*(int64_t*)(fields+0xd0),file==0);
    if(!open)_exit(5);
    const char* prefix=getenv("ENGINE_READER_OUTPUT");if(!prefix)_exit(7);
    char path[2048];snprintf(path,sizeof(path),"%s.stream.f32",prefix);
    FILE* stream=fopen(path,"wb");if(!stream)_exit(7);
    int64_t (*read_samples)(void*,void*,int64_t,uint64_t)=(int64_t(*)(void*,void*,int64_t,uint64_t))(engine_base+0x1612420);
    float output[4096];int64_t total=0;int complete=fresh!=0;
    for(int block=0;!fresh && block<300;block++){
      int64_t got=read_samples(reader,output,total,4096);
      if(got<0 || got>4096)_exit(8);
      if(!got){complete=1;break;}
      if(fwrite(output,sizeof(float),(size_t)got,stream)!=(size_t)got)_exit(9);
      total+=got;
    }
    fclose(stream);if(!complete)_exit(10);
    dprintf(1,"PCM_STREAM samples=%lld\n",(long long)total);
    int64_t positions[]={0,2,128,2304,88200,total-2,0,total+128,8190};
    size_t position_count=sizeof(positions)/sizeof(positions[0]);
    if(fresh){positions[0]=fresh_position;position_count=1;dprintf(1,"PCM_FRESH_READER position=%lld\n",(long long)fresh_position);}
    for(size_t i=0;i<position_count;i++){
      int64_t got=read_samples(reader,output,positions[i],256);
      if(got<0 || got>256)_exit(11);
      snprintf(path,sizeof(path),"%s.seek%zu.f32",prefix,i);
      FILE* f=fopen(path,"wb");if(!f)_exit(7);
      if(fwrite(output,sizeof(float),(size_t)got,f)!=(size_t)got)_exit(9);fclose(f);
      dprintf(1,"PCM_SEEK index=%zu position=%lld samples=%lld\n",i,(long long)positions[i],(long long)got);
    }
    dprintf(1,"PCM_PROBE PASS\n");_exit(0);
}
