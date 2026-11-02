/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
typedef int (*Entry)(int,char **);
int main(int argc,char **argv){
 if(argc<2)return 2;
#ifdef _WIN32
 HMODULE module=LoadLibraryA(argv[1]);if(!module){fprintf(stderr,"LoadLibrary failed: %lu\n",(unsigned long)GetLastError());return 3;}
 Entry entry=(Entry)GetProcAddress(module,"dma_transfer_fixture");
#else
 void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module){fprintf(stderr,"dlopen: %s\n",dlerror());return 3;}
 Entry entry=(Entry)dlsym(module,"dma_transfer_fixture");
#endif
 if(!entry)return 4;
 /* One fixture per process, kept mapped through runtime TLS teardown. */
 return entry(argc-1,argv+1);
}
