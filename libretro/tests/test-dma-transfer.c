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
typedef void BlockCompletionFunc(void *,int);
typedef struct BlockAIOCB BlockAIOCB;
typedef struct {size_t aiocb_size;void(*cancel_async)(BlockAIOCB *);} AIOCBInfo;
struct BlockAIOCB {BlockCompletionFunc *cb;void *opaque;};
typedef struct {void(*callback)(void *);void*opaque;bool registered;} AddressSpaceMapClient;
typedef struct {uintptr_t map_client_owner;} AddressSpace;
typedef struct {dma_addr_t base,len;} ScatterGatherEntry;
typedef struct {AddressSpace *as;ScatterGatherEntry *sg;int nsg;} QEMUSGList;
struct iovec {void *iov_base;size_t iov_len;};
typedef struct {struct iovec *iov;int niov,nalloc;size_t size;} QEMUIOVector;
typedef BlockAIOCB *DMAIOFunc(int64_t,QEMUIOVector *,BlockCompletionFunc *,void *,void *);
typedef struct {int unused;} AioContext;
static AioContext context;
static AioContext *qemu_get_current_aio_context(void){return &context;}
static void *qemu_aio_get(const AIOCBInfo *info,void *ctx,BlockCompletionFunc *cb,void *opaque){BlockAIOCB*a=calloc(1,info->aiocb_size);assert(a);a->cb=cb;a->opaque=opaque;return a;}
static void qemu_aio_unref(void *a){free(a);}
static void blk_aio_cancel_async(BlockAIOCB *a){(void)a;abort();}
static void qemu_iovec_reset(QEMUIOVector *v){v->size=0;v->niov=0;}
static void qemu_iovec_init(QEMUIOVector *v,int n){v->iov=calloc(n?n:1,sizeof(*v->iov));assert(v->iov);v->nalloc=n?n:1;}
static void qemu_iovec_add(QEMUIOVector *v,void *p,size_t n){assert(v->niov<v->nalloc);v->iov[v->niov++]=(struct iovec){p,n};v->size+=n;}
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
static BlockAIOCB pending;
static BlockAIOCB *submit_io(int64_t off,QEMUIOVector *v,BlockCompletionFunc *cb,void *arg,void *opaque){
 assert(!pending.cb&&off>=0);io_calls++;
 for(int i=0;i<v->niov;i++){assert((uint64_t)off+v->iov[i].iov_len<=sizeof(disk));if(write_disk)memcpy(disk+off,v->iov[i].iov_base,v->iov[i].iov_len);else memcpy(v->iov[i].iov_base,disk+off,v->iov[i].iov_len);off+=v->iov[i].iov_len;}
 pending.cb=cb;pending.opaque=arg;return &pending;
}
/* PRODUCTION */
static void complete(void *opaque,int ret){assert(!ret&&opaque==&calls);calls++;}
static void run(bool write,bool retry,bool overlap){
 AddressSpace as={sthread_get_current_thread_id()};ScatterGatherEntry sg[3]={{0,512},{overlap?256:1024,512},{2048,512}};QEMUSGList q={&as,sg,3};
 uint8_t expected[4096];for(unsigned i=0;i<sizeof(guest);i++){guest[i]=(uint8_t)(i*3+17);disk[i]=(uint8_t)(i*5+31);}memcpy(expected,write?disk:guest,sizeof(expected));
 for(unsigned i=0,off=0;i<3;i++){if(write)memcpy(expected+off,guest+sg[i].base,sg[i].len);else memcpy(expected+sg[i].base,disk+off,sg[i].len);off+=sg[i].len;}
 maps=unmaps=calls=io_calls=0;fail_map=retry;write_disk=write;
 dma_blk_io(&q,0,512,submit_io,NULL,complete,&calls,write?DMA_DIRECTION_TO_DEVICE:DMA_DIRECTION_FROM_DEVICE);
 assert(!calls);
 if(retry){assert(waiting&&!pending.cb);AddressSpaceMapClient*c=waiting;waiting=NULL;c->registered=false;fail_map=false;c->callback(c->opaque);}
 for(unsigned n=0;pending.cb;n++){assert(n<8);BlockCompletionFunc*cb=pending.cb;void*arg=pending.opaque;pending.cb=NULL;cb(arg,0);}
 assert(calls==1&&maps==unmaps&&!waiting&&io_calls==(overlap&&!write?2:1));
 assert(!memcmp(expected,write?disk:guest,sizeof(expected)));
}
int main(void){for(unsigned n=0;n<1000;n++)for(unsigned w=0;w<2;w++)for(unsigned r=0;r<2;r++)for(unsigned o=0;o<2;o++)run(w,r,o);free(dma_iov_spare);puts("DMA transfer: 8000 reads/writes, retries, overlapping SG groups, exact bytes and balanced maps PASS");}
