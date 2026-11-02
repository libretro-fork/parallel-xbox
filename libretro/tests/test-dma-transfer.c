/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <rthreads/rthreads.h>
#include "qemu/queue.h"
#include <time.h>
#define g_free free
#define trace_dma_complete(...) ((void)0)
#define trace_dma_blk_cb(...) ((void)0)
#define trace_dma_map_wait(...) ((void)0)
#define trace_dma_aio_cancel(...) ((void)0)
#define trace_dma_blk_io(...) ((void)0)
#define MEMTXATTRS_UNSPECIFIED 0
#define QEMU_IS_ALIGNED(n,a) (((n)&((a)-1))==0)
#define QEMU_ALIGN_DOWN(n,a) ((n)&~((a)-1))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef uint64_t dma_addr_t;
typedef enum {DMA_DIRECTION_TO_DEVICE,DMA_DIRECTION_FROM_DEVICE} DMADirection;
typedef struct BlockDriverState {int unused;} BlockDriverState;
/* AIOCB TYPES */
/* CLIENT TYPE */
typedef struct {uintptr_t map_client_owner;} AddressSpace;
typedef struct {dma_addr_t base,len;} ScatterGatherEntry;
typedef struct {AddressSpace *as;ScatterGatherEntry *sg;int nsg;} QEMUSGList;
struct iovec {void *iov_base;size_t iov_len;};
#define QEMU_BUILD_BUG_ON(c) _Static_assert(!(c),"iov layout")
/* IOV TYPE */
typedef BlockAIOCB *DMAIOFunc(int64_t,QEMUIOVector *,BlockCompletionFunc *,void *,void *);
typedef struct {int unused;} AioContext;
static AioContext context;
static AioContext *qemu_get_current_aio_context(void){return &context;}
static unsigned request_allocations,request_frees;
static void *request_malloc(size_t n){void*p=malloc(n);assert(p);request_allocations++;return p;}
static void request_free(void*p){if(p){request_frees++;free(p);}}
#define g_malloc request_malloc
#undef g_free
#define g_free request_free
/* REQUEST CACHE */
static unsigned vector_allocations,vector_frees,vector_growths;
static void *vector_malloc(size_t n){if(!n)return NULL;void*p=malloc(n);assert(p);vector_allocations++;return p;}
static void *vector_realloc(void*p,size_t n){if(!p)vector_allocations++;vector_growths++;p=realloc(p,n);assert(p);return p;}
#define g_new(t,n) ((t*)vector_malloc(sizeof(t)*(n)))
#define g_renew(t,p,n) ((t*)vector_realloc(p,sizeof(t)*(n)))
static void __attribute__((noinline)) vector_free(void*p){if(p){vector_frees++;free(p);}}
#undef g_free
#define g_free vector_free
static void blk_aio_cancel_async(BlockAIOCB *a){(void)a;abort();}
static void qemu_iovec_reset(QEMUIOVector *v){v->size=0;v->niov=0;}
/* IOV IMPLEMENTATION */
static void qemu_iovec_discard_back(QEMUIOVector *v,size_t n){(void)v;(void)n;abort();}
static bool ranges_overlap(uintptr_t a,size_t n,uintptr_t b,size_t m){return a<b+m&&b<a+n;}
static bool icount_enabled(void){return true;}
static uint8_t guest[4096],disk[4096];
static unsigned maps,unmaps,calls,io_calls;
static bool fail_map,write_disk;
static AddressSpaceMapClient *waiting;
static void *dma_memory_map(AddressSpace *as,dma_addr_t addr,dma_addr_t *n,DMADirection dir,int attrs){assert(as->map_client_owner==sthread_get_current_thread_id());if(fail_map)return NULL;assert(addr+*n<=sizeof(guest));maps++;return guest+addr;}
static void dma_memory_unmap(AddressSpace *as,void *p,dma_addr_t n,DMADirection dir,dma_addr_t done){assert(as->map_client_owner==sthread_get_current_thread_id()&&p&&n==done);unmaps++;}
static void address_space_register_map_client(AddressSpace *as,AddressSpaceMapClient *c,void(*cb)(void *),void *arg){assert(!c->registered&&!waiting);c->registered=true;c->callback=cb;c->opaque=arg;waiting=c;}
static void address_space_unregister_map_client(AddressSpace *as,AddressSpaceMapClient *c){assert(waiting==c);c->registered=false;waiting=NULL;}
static BlockAIOCB pending;static int io_error,pending_error,expected_error;static bool close_inside;
static BlockAIOCB *submit_io(int64_t off,QEMUIOVector *v,BlockCompletionFunc *cb,void *arg,void *opaque){
 assert(!pending.cb&&off>=0);io_calls++;
 for(int i=0;i<v->niov;i++){assert((uint64_t)off+v->iov[i].iov_len<=sizeof(disk));if(write_disk)memcpy(disk+off,v->iov[i].iov_base,v->iov[i].iov_len);else memcpy(v->iov[i].iov_base,disk+off,v->iov[i].iov_len);off+=v->iov[i].iov_len;}
 pending.cb=cb;pending.opaque=arg;pending_error=io_error;return &pending;
}
/* PRODUCTION */
static void cleanup_iov(void){
#ifdef DMA_OWNER_DESCRIPTORS
 lr_dma_iov_cache_clear();
#else
 vector_free(dma_iov_spare);dma_iov_spare=NULL;dma_iov_spare_alloc=0;
#endif
}
static void complete(void *opaque,int ret){assert(ret==expected_error&&opaque==&calls);calls++;if(close_inside){cleanup_iov();lr_aiocb_cache_clear();}}
static void finish_pending(void){assert(pending.cb);BlockCompletionFunc*cb=pending.cb;void*arg=pending.opaque;int error=pending_error;pending.cb=NULL;cb(arg,error);}

static void run(bool write,bool retry,bool overlap){
 AddressSpace as={sthread_get_current_thread_id()};ScatterGatherEntry sg[3]={{0,512},{overlap?256:1024,512},{2048,512}};QEMUSGList q={&as,sg,3};
 uint8_t expected[4096];for(unsigned i=0;i<sizeof(guest);i++){guest[i]=(uint8_t)(i*3+17);disk[i]=(uint8_t)(i*5+31);}memcpy(expected,write?disk:guest,sizeof(expected));
 for(unsigned i=0,off=0;i<3;i++){if(write)memcpy(expected+off,guest+sg[i].base,sg[i].len);else memcpy(expected+sg[i].base,disk+off,sg[i].len);off+=sg[i].len;}
 maps=unmaps=calls=io_calls=0;fail_map=retry;write_disk=write;io_error=expected_error=0;
 dma_blk_io(&q,0,512,submit_io,NULL,complete,&calls,write?DMA_DIRECTION_TO_DEVICE:DMA_DIRECTION_FROM_DEVICE);
 assert(!calls);
 if(retry){assert(waiting&&!pending.cb);AddressSpaceMapClient*c=waiting;waiting=NULL;c->registered=false;fail_map=false;c->callback(c->opaque);}
 for(unsigned n=0;pending.cb;n++){assert(n<8);finish_pending();}
 assert(calls==1&&maps==unmaps&&!waiting&&io_calls==(overlap&&!write?2:1));
 assert(!memcmp(expected,write?disk:guest,sizeof(expected)));
#ifdef DMA_OWNER_DESCRIPTORS
 if(close_inside)assert(!dma_iov_spare&&!dma_iov_spare_alloc);
#endif
}

#ifdef DMA_CANCEL_RELEASE
static void cancel_wait_case(bool close){
 cleanup_iov();lr_aiocb_cache_clear();AddressSpace as={sthread_get_current_thread_id()};ScatterGatherEntry entry={0,512};QEMUSGList sg={&as,&entry,1};
 maps=unmaps=calls=0;fail_map=true;write_disk=false;close_inside=close;expected_error=-ECANCELED;io_error=0;
 BlockAIOCB*acb=dma_blk_io(&sg,0,512,submit_io,NULL,complete,&calls,DMA_DIRECTION_FROM_DEVICE);
 assert(waiting&&!pending.cb&&acb->refcnt==1);qemu_aio_ref(acb);dma_aio_cancel(acb);
 assert(calls==1&&!waiting&&maps==unmaps);assert(acb->refcnt==1);
 qemu_aio_unref(acb);
 fail_map=false;close_inside=false;expected_error=0;
 unsigned before=vector_allocations;BlockAIOCB*next=dma_blk_io(&sg,0,512,submit_io,NULL,complete,&calls,DMA_DIRECTION_FROM_DEVICE);
 if(!close){assert(next==acb&&vector_allocations==before);}
 finish_pending();assert(calls==2&&maps==unmaps);
 cleanup_iov();lr_aiocb_cache_clear();assert(request_allocations==request_frees&&vector_allocations==vector_frees);
 puts("DMA cancelled map wait: callback once, completion reference released, descriptor/request frees balanced PASS");
}
#endif
static AddressSpace chain_as;static ScatterGatherEntry chain_entry={0,512};static QEMUSGList chain_sg={&chain_as,&chain_entry,1};static unsigned chain_left;static bool chain_nested;
static void chain_done(void *opaque,int ret){
 assert(!ret&&maps==unmaps);assert(!memcmp(guest,disk,512));calls++;
 if(--chain_left){dma_blk_io(&chain_sg,0,512,submit_io,NULL,chain_done,NULL,DMA_DIRECTION_FROM_DEVICE);if(chain_nested)finish_pending();}
 else if(close_inside){cleanup_iov();lr_aiocb_cache_clear();}
}
static void chain_case(void){
 cleanup_iov();lr_aiocb_cache_clear();chain_as.map_client_owner=sthread_get_current_thread_id();maps=unmaps=calls=0;fail_map=write_disk=false;io_error=expected_error=0;chain_left=1000;
 for(unsigned i=0;i<512;i++)disk[i]=(uint8_t)(i*17+5);
 unsigned before=vector_allocations;dma_blk_io(&chain_sg,0,512,submit_io,NULL,chain_done,NULL,DMA_DIRECTION_FROM_DEVICE);
 while(pending.cb){finish_pending();}
 assert(calls==1000&&maps==unmaps);
#ifdef DMA_OWNER_DESCRIPTORS
 assert(vector_allocations-before==1);
#else
 assert(vector_allocations-before==2);
#endif
 printf("DMA callback chain: 1000 transfers, descriptor allocations=%u PASS\n",vector_allocations-before);cleanup_iov();lr_aiocb_cache_clear();
}
static ScatterGatherEntry growth_entries[64];static QEMUSGList growth_sg;
static void growth_done(void *opaque,int ret){
 assert(!ret&&maps==unmaps);calls++;
 if(calls==1)dma_blk_io(&growth_sg,0,512,submit_io,NULL,growth_done,NULL,DMA_DIRECTION_FROM_DEVICE);
}
static void growth_case(void){
 cleanup_iov();lr_aiocb_cache_clear();chain_as.map_client_owner=sthread_get_current_thread_id();chain_entry.len=512;
 for(unsigned i=0;i<64;i++)growth_entries[i]=(ScatterGatherEntry){i*64,64};
 growth_sg=(QEMUSGList){&chain_as,growth_entries,64};
 maps=unmaps=calls=0;fail_map=write_disk=close_inside=false;io_error=expected_error=0;
 unsigned before=vector_growths;
 dma_blk_io(&chain_sg,0,512,submit_io,NULL,growth_done,NULL,DMA_DIRECTION_FROM_DEVICE);
 while(pending.cb)finish_pending();
 assert(calls==2&&maps==unmaps&&!memcmp(guest,disk,sizeof(guest)));
#ifdef DMA_OWNER_DESCRIPTORS
 assert(vector_growths-before==1);
#else
 assert(vector_growths==before);
#endif
 cleanup_iov();lr_aiocb_cache_clear();assert(vector_allocations==vector_frees&&request_allocations==request_frees);
 puts("DMA callback growth: 1 to 64 descriptors, exact bytes, bounded reserve PASS");
}
static void nested_close_case(void){
 cleanup_iov();lr_aiocb_cache_clear();chain_as.map_client_owner=sthread_get_current_thread_id();chain_entry.len=512;maps=unmaps=calls=0;fail_map=write_disk=false;io_error=expected_error=0;chain_left=4;chain_nested=close_inside=true;
 unsigned before=vector_allocations;dma_blk_io(&chain_sg,0,512,submit_io,NULL,chain_done,NULL,DMA_DIRECTION_FROM_DEVICE);finish_pending();
 assert(!pending.cb&&calls==4&&maps==unmaps);
#ifdef DMA_OWNER_DESCRIPTORS
 assert(vector_allocations-before==1&&!dma_iov_spare);
#else
 assert(vector_allocations-before==4);
#endif
 cleanup_iov();lr_aiocb_cache_clear();assert(request_allocations==request_frees&&vector_allocations==vector_frees);chain_nested=close_inside=false;
 puts("DMA nested callbacks: four levels, close at leaf, all request/vector frees balanced PASS");
}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
static BlockAIOCB *(*volatile dma_start)(QEMUSGList *,uint64_t,uint32_t,DMAIOFunc *,void *,BlockCompletionFunc *,void *,DMADirection)=dma_blk_io;
static void benchmark(void){
 chain_as.map_client_owner=sthread_get_current_thread_id();fail_map=write_disk=close_inside=false;io_error=expected_error=0;
 for(unsigned mode=0;mode<2;mode++){
  unsigned loops=500000;chain_entry.len=mode?2048:512;maps=unmaps=calls=0;
  dma_blk_io(&chain_sg,0,512,submit_io,NULL,complete,&calls,DMA_DIRECTION_FROM_DEVICE);finish_pending();
  unsigned before=vector_allocations;double start=now();
  for(unsigned n=0;n<loops;n++){dma_start(&chain_sg,0,512,submit_io,NULL,complete,&calls,DMA_DIRECTION_FROM_DEVICE);finish_pending();__asm__ __volatile__("" : : "r"(guest) : "memory");}
  assert(maps==unmaps&&calls==loops+1);printf("mode=%u ns=%.3f vector_allocations=%u\n",mode,(now()-start)/loops,vector_allocations-before);
 }
}
#ifdef DMA_SHARED_BENCH
#ifdef _WIN32
__declspec(dllexport)
#endif
int dma_transfer_fixture(int argc,char **argv){
#else
int main(int argc,char **argv){
#endif
 if(argc>1&&!strcmp(argv[1],"--bench"))benchmark();
 else{
  for(unsigned n=0;n<1000;n++){close_inside=(n&1)!=0;for(unsigned w=0;w<2;w++)for(unsigned r=0;r<2;r++)for(unsigned o=0;o<2;o++)run(w,r,o);}
  close_inside=false;chain_case();nested_close_case();growth_case();
#ifdef DMA_CANCEL_RELEASE
  cancel_wait_case(false);cancel_wait_case(true);
#endif
  chain_as.map_client_owner=sthread_get_current_thread_id();chain_entry.len=512;maps=unmaps=calls=0;io_error=expected_error=-EIO;close_inside=true;dma_blk_io(&chain_sg,0,512,submit_io,NULL,complete,&calls,DMA_DIRECTION_FROM_DEVICE);finish_pending();assert(calls==1&&maps==unmaps);
#ifdef DMA_OWNER_DESCRIPTORS
  assert(!dma_iov_spare);
#endif
  puts("DMA transfer: 8000 reads/writes, retry/overlap bytes, callback close, error completion and balanced maps PASS");
 }
 cleanup_iov();lr_aiocb_cache_clear();assert(vector_allocations==vector_frees&&request_allocations==request_frees);return 0;
}
