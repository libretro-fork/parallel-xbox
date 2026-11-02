/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct BlockDriverState {int tag;} BlockDriverState;
/* TYPES */
typedef struct Notifier Notifier;
struct Notifier {void (*notify)(Notifier *,void *);};
static unsigned registrations,allocations,frees,callbacks;
static void qemu_thread_atexit_add(Notifier*n){assert(n->notify);registrations++;}
static void *allocate(size_t n){void*p=malloc(n);assert(p);allocations++;return p;}
static void release(void*p){assert(p);frees++;free(p);}
#define g_malloc allocate
#define g_free release
/* PRODUCTION */
static void clear_cache(void){
#ifdef LEGACY
 aiocb_cache_free(NULL,NULL);
#else
 lr_aiocb_cache_clear();
#endif
}
static void done(void *opaque,int ret){assert(opaque==(void *)(uintptr_t)0x1234&&ret==7);callbacks++;}
static AIOCBInfo info[9];static BlockDriverState states[9];
static void verify(BlockAIOCB*a,unsigned k){assert(a->aiocb_info==&info[k]&&a->bs==&states[k]&&a->cb==done&&a->opaque==(void *)(uintptr_t)0x1234&&a->refcnt==1);}
static BlockAIOCB *get(unsigned k){BlockAIOCB*a=qemu_aio_get(&info[k],&states[k],done,(void *)(uintptr_t)0x1234);verify(a,k);return a;}
static void balanced(void){clear_cache();clear_cache();assert(allocations==frees);}
static void lifecycle(void){
 balanced();
 BlockAIOCB*a=get(0);qemu_aio_ref(a);qemu_aio_ref(a);qemu_aio_unref(a);assert(a->refcnt==2);qemu_aio_unref(a);assert(a->refcnt==1);a->cb(a->opaque,7);a->bs=NULL;a->cb=NULL;a->opaque=NULL;qemu_aio_unref(a);assert(callbacks==1);
 unsigned before=allocations;BlockAIOCB*b=get(0);assert(b==a&&allocations==before);qemu_aio_unref(b);balanced();
 BlockAIOCB*many[9][16];
 for(unsigned k=0;k<9;k++)for(unsigned n=0;n<16;n++)many[k][n]=get(k);
 for(unsigned k=0;k<9;k++)for(unsigned n=0;n<16;n++)qemu_aio_unref(many[k][n]);
 assert(allocations-frees==AIOCB_CACHE_KINDS*AIOCB_CACHE_DEPTH);balanced();
 a=get(0);qemu_aio_ref(a);clear_cache();assert(a->refcnt==2);qemu_aio_unref(a);assert(a->refcnt==1);qemu_aio_unref(a);
#ifndef LEGACY
 assert(allocations==frees); /* a completion after close cannot refill it */
#endif
 balanced();
 a=get(0);clear_cache();b=get(1);qemu_aio_unref(b);qemu_aio_unref(a);assert(allocations-frees==2);balanced(); /* new submission re-enables safe metadata reuse */
 for(unsigned n=0;n<10000;n++){unsigned k=n%2;a=get(k);qemu_aio_ref(a);qemu_aio_unref(a);verify(a,k);qemu_aio_unref(a);}
 before=allocations;for(unsigned n=0;n<10000;n++){a=get(n%2);qemu_aio_unref(a);}assert(allocations==before);balanced();
#ifndef LEGACY
 assert(!registrations);
#endif
 puts("AIOCB owner cache: metadata reset, refs, eight-kind/depth bounds, overflow fallback, close with live refs, reopen and 20000 warm cycles PASS");
}
static void *(*volatile get_fn)(const AIOCBInfo *,BlockDriverState *,BlockCompletionFunc *,void *)=qemu_aio_get;
static void (*volatile unref_fn)(void *)=qemu_aio_unref;
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
static void benchmark(void){
 for(unsigned mode=0;mode<5;mode++){
  clear_cache();unsigned kinds=mode==0?1:mode==4?9:2,batch=mode==2?8:mode==3?16:1,loops=500000;
  BlockAIOCB*a[16];
  for(unsigned k=0;k<kinds;k++){for(unsigned n=0;n<batch;n++)a[n]=get(k);for(unsigned n=0;n<batch;n++)qemu_aio_unref(a[n]);}
  unsigned before=allocations;double start=now();
  for(unsigned n=0;n<loops;n++){
   unsigned k=n%kinds;for(unsigned j=0;j<batch;j++)a[j]=get_fn(&info[k],&states[k],done,(void *)(uintptr_t)0x1234);
   for(unsigned j=0;j<batch;j++)unref_fn(a[j]);
  }
  printf("mode=%u ns=%.3f allocations=%u\n",mode,(now()-start)/loops,allocations-before);
 }
 balanced();
}
int main(int argc,char **argv){for(unsigned k=0;k<9;k++)info[k].aiocb_size=sizeof(BlockAIOCB)+16*(k+1);info[0].aiocb_size=192;info[1].aiocb_size=88;if(argc>1&&!strcmp(argv[1],"--bench"))benchmark();else lifecycle();return 0;}
