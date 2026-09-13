#define _GNU_SOURCE
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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
/* Inspection only: deliberately do not call the original main or reader yet.
 * Offsets apply only to the SHA256 documented in the investigation note. */
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
    _exit(good?0:3);
}
