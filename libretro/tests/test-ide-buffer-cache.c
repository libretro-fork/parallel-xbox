/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "qemu/queue.h"
struct iovec {void *iov_base;size_t iov_len;};
#define QEMU_BUILD_BUG_ON(c) _Static_assert(!(c),"iov layout")
#define BDRV_SECTOR_BITS 9
/* QIOV */
typedef void BlockCompletionFunc(void *,int);
typedef struct {BlockCompletionFunc *cb;void *opaque;int ret;bool live,completing;} BlockAIOCB;
/* REQUEST */
typedef struct {QLIST_HEAD(,IDEBufferedRequest) buffered_requests;void *blk;} IDEState;
static unsigned req_allocs,req_frees,buf_allocs,buf_frees,provider_reads,copy_calls,calls;

static void *new_request(size_t n){req_allocs++;void*p=calloc(1,n);assert(p);return p;}
static void free_request(void*p){if(p){req_frees++;free(p);}}
#define g_new0(t,n) ((t *)new_request(sizeof(t)*(n)))
#define g_free free_request
#ifdef _WIN32
#include <malloc.h>
#endif
static void *blk_blockalign(void *blk,size_t n){
 void*p;
#ifdef _WIN32
 p=_aligned_malloc(n?n:1,4096);
#else
 assert(!posix_memalign(&p,4096,n?n:1));
#endif
 assert(p);buf_allocs++;return p;
}
static void qemu_vfree(void*p){if(p){buf_frees++;
#ifdef _WIN32
 _aligned_free(p);
#else
 free(p);
#endif
}}
static size_t qemu_iovec_from_buf(QEMUIOVector*v,size_t off,const void*src,size_t n){assert(!off);copy_calls++;size_t left=n;for(int i=0;i<v->niov&&left;i++){size_t k=v->iov[i].iov_len<left?v->iov[i].iov_len:left;memcpy(v->iov[i].iov_base,src,k);src=(const uint8_t*)src+k;left-=k;}assert(!left);return n;}
static BlockAIOCB pending[64];static int provider_error;
static BlockAIOCB *blk_aio_preadv(void*blk,int64_t off,QEMUIOVector*v,int flags,BlockCompletionFunc*cb,void*arg){
 assert(!((uintptr_t)qemu_iovec_buf(v)&4095));provider_reads++;memset(qemu_iovec_buf(v),0x55,v->size);
 for(unsigned i=0;i<64;i++)if(!pending[i].live){pending[i]=(BlockAIOCB){.cb=cb,.opaque=arg,.ret=provider_error,.live=true};return &pending[i];}
 abort();
}
static BlockAIOCB *blk_abort_aio_request(void*blk,BlockCompletionFunc*cb,void*arg,int ret){(void)blk;(void)cb;(void)arg;(void)ret;abort();}
/* PRODUCTION */
static IDEState state;
static uint8_t destination[131072];
static int expected;static bool clear_inside,submit_inside;
static QEMUIOVector nested;
static void complete(void*arg,int ret){
 assert(arg==&state&&ret==expected);calls++;
 if(clear_inside){clear_inside=false;ide_buffered_read_cache_clear();}
 if(submit_inside){submit_inside=false;qemu_iovec_init_buf(&nested,destination+65536,512);ide_buffered_readv(&state,0,&nested,1,complete,&state);}
}
static void finish(BlockAIOCB*a){
 assert(a->live&&!a->completing);BlockCompletionFunc*cb=a->cb;void*p=a->opaque;int ret=a->ret;
 /* Production fixed slots stay busy until their callback returns. */
 a->completing=true;cb(p,ret);a->completing=false;a->live=false;
}
static BlockAIOCB *(*volatile read_fn)(IDEState *,int64_t,QEMUIOVector *,int,BlockCompletionFunc *,void *)=ide_buffered_readv;
static void finish_all(void){for(unsigned n=0;n<64;n++)if(pending[n].live&&!pending[n].completing)finish(&pending[n]);assert(QLIST_EMPTY(&state.buffered_requests));}
static void check(size_t bytes,int error,bool orphan,bool close,bool nested_submit){
 memset(destination,0xcc,sizeof(destination));struct iovec pieces[3]={{destination,bytes/2},{destination+bytes/2+16,bytes-bytes/2},{NULL,0}};
 QEMUIOVector q={.iov=pieces,.niov=3,.nalloc=-1,.size=bytes};
 unsigned copies=copy_calls,before=calls;provider_error=error;expected=error;clear_inside=close;submit_inside=nested_submit;
 BlockAIOCB*a=ide_buffered_readv(&state,0,&q,(int)(bytes>>9),complete,&state);
 assert(calls==before&&!memcmp(destination,(uint8_t[1]){0xcc},1));
 if(orphan)QLIST_FIRST(&state.buffered_requests)->orphaned=true;
 finish(a);finish_all();
#ifdef HAVE_IDE_BUFFER_CACHE
 if(close&&!nested_submit)assert(!ide_read_cache.spare);
#endif
 assert(calls==before+(orphan?0u:nested_submit?2u:1u));
 for(size_t i=0;i<bytes+16;i++){bool touched=i<bytes/2||i>=bytes/2+16;assert(destination[i]==((!orphan&&!error&&touched)?0x55:0xcc));}
 assert(copy_calls==copies+((!error&&!orphan)?1u:0u)+(nested_submit?1u:0u));
}
#ifdef HAVE_IDE_BUFFER_CACHE
static QEMUIOVector chain_qiov;
static unsigned chain_left;
static bool chain_nested;
static void chain_complete(void*opaque,int ret){
 assert(opaque==&state&&!ret);calls++;
 for(unsigned i=0;i<512;i++)assert(destination[i]==0x55);
 if(--chain_left){
  BlockAIOCB*next=ide_buffered_readv(&state,0,&chain_qiov,1,chain_complete,&state);
  if(chain_nested)finish(next);
 }else ide_buffered_read_cache_clear();
}
static void chain_check(bool nested_completion){
 ide_buffered_read_cache_clear();assert(QLIST_EMPTY(&state.buffered_requests));
 qemu_iovec_init_buf(&chain_qiov,destination,512);provider_error=0;
 chain_nested=nested_completion;chain_left=nested_completion?4:1000;
 unsigned before=req_allocs,buffers=buf_allocs,done=calls,want=chain_left;
 BlockAIOCB*first=ide_buffered_readv(&state,0,&chain_qiov,1,chain_complete,&state);
 finish(first);
 while(chain_left){bool found=false;for(unsigned i=0;i<64;i++)if(pending[i].live&&!pending[i].completing){finish(&pending[i]);found=true;break;}assert(found);}
 assert(calls==done+want&&QLIST_EMPTY(&state.buffered_requests));
#ifdef IDE_RELEASE_BEFORE_CALLBACK
 assert(req_allocs-before==1&&buf_allocs-buffers==1);
#else
 assert(req_allocs-before==(nested_completion?4u:2u));
 assert(buf_allocs-buffers==req_allocs-before);
#endif
 assert(!ide_read_cache.spare&&req_allocs==req_frees&&buf_allocs==buf_frees);
 printf("IDE callback chain: nested=%d completions=%u requests=%u buffers=%u; leaf close and balanced frees PASS\n",nested_completion,want,req_allocs-before,buf_allocs-buffers);
}
#endif
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
int main(int argc,char **argv){

 if(argc>1&&!strcmp(argv[1],"--bench")){
  for(unsigned mode=0;mode<3;mode++){
   size_t size=mode==0?512:mode==1?2048:65536;unsigned loops=100000;QEMUIOVector q;
   qemu_iovec_init_buf(&q,destination,size);provider_error=expected=0;
   finish(ide_buffered_readv(&state,0,&q,(int)(size>>9),complete,&state)); /* warm */
   unsigned ra=req_allocs,ba=buf_allocs;double start=now();
   for(unsigned n=0;n<loops;n++){finish(read_fn(&state,0,&q,(int)(size>>9),complete,&state));__asm__ __volatile__("" : : "r"(destination) : "memory");}
   printf("size=%zu ns=%.3f request_allocs=%u buffer_allocs=%u\n",size,(now()-start)/loops,req_allocs-ra,buf_allocs-ba);
  }
 }else{
  for(unsigned n=0;n<1000;n++){
   check(0,0,false,false,false);
   check(512,0,false,false,false);check(2048,0,false,false,false);check(65536,0,false,false,false);check(512,-EIO,false,false,false);check(2048,0,true,false,false);check(512,0,false,true,false);check(512,0,false,false,true);check(512,0,false,true,true);
  }
#ifdef HAVE_IDE_BUFFER_CACHE
  chain_check(false);chain_check(true);
#endif
  ide_buffered_read_cache_clear();QEMUIOVector q[3];BlockAIOCB *a[3];size_t sizes[3]={2048,65536,512};
  expected=provider_error=0;
  for(unsigned i=0;i<3;i++){qemu_iovec_init_buf(&q[i],destination,sizes[i]);a[i]=ide_buffered_readv(&state,0,&q[i],(int)(sizes[i]>>9),complete,&state);}
  for(int i=2;i>=0;i--)finish(a[i]);
#ifdef HAVE_IDE_BUFFER_CACHE
  assert(ide_read_cache.spare&&ide_read_cache.spare->buffer_capacity==65536);
#endif
  unsigned before=req_allocs;check(512,0,false,false,false);check(512,0,false,false,false);
#ifdef HAVE_IDE_BUFFER_CACHE
  assert(req_allocs==before);
#endif
  printf("IDE buffered reads: bytes/gaps, errors, orphans, callback close/reentry, growth/shrink PASS; final request allocations=%u\n",req_allocs-before);
 }
 ide_buffered_read_cache_clear();assert(req_allocs==req_frees&&buf_allocs==buf_frees);return 0;
}
