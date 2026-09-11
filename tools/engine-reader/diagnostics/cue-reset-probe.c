#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
static void fault(int sig){dprintf(2,"CUE_PROBE signal=%d\n",sig);_exit(90);}
static uintptr_t engine_base;
static int locate(struct dl_phdr_info* info, size_t size, void* opaque) {
    (void)size; (void)opaque;
    const char* name=info->dlpi_name;
    if (name && (!*name || strstr(name,"/usr/Engine/Engine"))) {
        engine_base=info->dlpi_addr;
        return 1;
    }
    return 0;
}
/* Exercise the real CueData constructor and methods; never call Engine main.
 * Offsets apply only to the executable hash enforced by check-cue-reset.py.
 * No database is loaded, and object teardown is left to process exit. */
int __libc_start_main(int (*main_fn)(int,char**,char**),int argc,char** argv,
        void (*init)(void),void (*fini)(void),void (*rtld_fini)(void),void* stack) {
    (void)main_fn;(void)argc;(void)argv;(void)init;(void)fini;(void)rtld_fini;(void)stack;
    dl_iterate_phdr(locate,0);
    if (!engine_base) {dprintf(2,"ENTRY_PROBE missing Engine mapping\n");_exit(2);}
    const char* type=(const char*)(engine_base+0x25a4ea0);
    uintptr_t reader=*(uintptr_t*)(engine_base+0x2ae1c70);
    int good=!strcmp(type,"16ffmpegFileReader") && reader==engine_base+0x1612420;
    dprintf(1,"ENTRY_PROBE base=%#lx type=%s read_wrapper=%#lx expected=%#lx\n",
            (unsigned long)engine_base,type,(unsigned long)reader,
            (unsigned long)(engine_base+0x1612420));
    dprintf(1,"ENTRY_PROBE %s; Engine main and decoder not invoked\n",good?"PASS":"FAIL");
    if(!good)_exit(3);
    signal(SIGSEGV,fault);signal(SIGABRT,fault);signal(SIGBUS,fault);
    void* (*allocate)(size_t)=(void*(*)(size_t))(engine_base+0x3915d8);
    void (*ctor)(void*,uint64_t)=(void(*)(void*,uint64_t))(engine_base+0x182b4c0);
    double (*get_main)(void*)=(double(*)(void*))(engine_base+0x15ed3b0);
    void (*set_main)(void*,double,int)=(void(*)(void*,double,int))(engine_base+0x15f4000);
    void (*set_secondary)(void*,double)=(void(*)(void*,double))(engine_base+0x15ed400);
    void (*set_flag)(void*,int)=(void(*)(void*,int))(engine_base+0x15ed3e0);
    int (*get_flag)(void*)=(int(*)(void*))(engine_base+0x15ed3ec);
    void (*reset)(void*)=(void(*)(void*))(engine_base+0x15f4100);
    void* cue=allocate(0x40);ctor(cue,8);
    if(*(uintptr_t*)cue!=engine_base+0x2b38070)_exit(4);
    dprintf(1,"CUE_INIT main=%.17g flag=%d\n",get_main(cue),get_flag(cue));
    if(get_main(cue)!=-1 || get_flag(cue)!=0)_exit(5);
    const double secondary_values[]={12345.5,0};const double main_values[]={45678.25,-1};
    for(int i=0;i<2;i++) {
      set_secondary(cue,secondary_values[i]);set_main(cue,main_values[i],0);set_flag(cue,1);
      if(get_main(cue)!=main_values[i] || get_flag(cue)!=1)_exit(6);
      set_flag(cue,0);
      if(get_main(cue)!=main_values[i])_exit(7);
      set_flag(cue,1);reset(cue);
      dprintf(1,"CUE_RESET index=%d secondary=%.17g before=%.17g after=%.17g flag=%d\n",i,secondary_values[i],main_values[i],get_main(cue),get_flag(cue));
      if(get_main(cue)!=secondary_values[i] || get_flag(cue)!=0)_exit(8);
    }
    dprintf(1,"CUE_PROBE PASS; real constructor and native methods; no database load or decoder\n");
    _exit(0);
}
