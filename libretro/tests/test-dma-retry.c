/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include "qemu/queue.h"
typedef struct QEMUBH {void (*cb)(void*);void *opaque;retro_atomic_int_t flags;} QEMUBH;
/* CLIENT */
typedef struct AddressSpace {
 uintptr_t map_client_owner;retro_atomic_ptr_t map_client_bh;
 size_t max_bounce_buffer_size;retro_atomic_64_t bounce_buffer_size;
 QLIST_HEAD(,AddressSpaceMapClient) map_client_list;
} AddressSpace;
static QEMUBH *all[16];static unsigned bh_allocs;
#define LR_DSP_EXTERNAL_GUARD ((void)0)
#define LR_DSP_FENCE_POLL 1
static void lr_dsp_ownership_fence(unsigned reason){assert(reason==1);}
static QEMUBH *qemu_bh_new(void(*cb)(void*),void *opaque){
 QEMUBH *b=calloc(1,sizeof(*b));assert(b);b->cb=cb;b->opaque=opaque;
 for(unsigned i=0;i<16;i++)if(!all[i]){all[i]=b;bh_allocs++;return b;}
 abort();
}
static void qemu_bh_schedule(QEMUBH*b){retro_atomic_fetch_or_int(&b->flags,1);}
static void qemu_bh_delete(QEMUBH*b){retro_atomic_fetch_or_int(&b->flags,2);}
static void run_ready(void){
 for(unsigned pass=0;pass<32;pass++){
  bool progress=false;
  for(unsigned i=0;i<16;i++)if(all[i]){
   QEMUBH*b=all[i];int flags=retro_atomic_exchange_int(&b->flags,0);
   if(flags&2){all[i]=NULL;free(b);progress=true;}
   else if(flags&1){b->cb(b->opaque);progress=true;}
  }
  if(!progress)return;
 }
 abort();
}
static retro_atomic_int_t go,done;
static unsigned hook;
static void await(retro_atomic_int_t *p,int value){while(retro_atomic_load_acquire_int(p)<value)sthread_yield();}
static void test_stage(unsigned stage){if(hook==stage){retro_atomic_store_release_int(&go,1);await(&done,1);}}
static retro_atomic_int_t release_hit,release_resume;
static bool pause_release;
static void test_release_stage(void){if(pause_release){retro_atomic_store_release_int(&release_hit,1);await(&release_resume,1);}}
/* PRODUCTION */
static void release_resources(AddressSpace *as,int64_t amount){
/* RELEASE */
}
static void initialize(AddressSpace*as){
 *as=(AddressSpace){.map_client_owner=sthread_get_current_thread_id(),.max_bounce_buffer_size=4};
}
static unsigned callbacks;
static void callback(void *opaque){AddressSpace*as=opaque;assert(as->map_client_owner==sthread_get_current_thread_id());callbacks++;}
static void release_once(void*opaque){await(&go,1);release_resources(opaque,4);retro_atomic_store_release_int(&done,1);}
static void cleanup(AddressSpace*as,AddressSpaceMapClient*client){
 assert(QLIST_EMPTY(&as->map_client_list));address_space_cancel_map_client_bh(as);(void)client;run_ready();
}
static void phase_cases(void){
 for(unsigned phase=1;phase<=4;phase++)for(unsigned n=0;n<100;n++){
  AddressSpace as;initialize(&as);AddressSpaceMapClient node={0};AddressSpaceMapClient*b=&node;callbacks=0;
  retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);retro_atomic_store_relaxed_int(&go,0);retro_atomic_store_relaxed_int(&done,0);hook=phase;
  sthread_t*t=sthread_create(release_once,&as);assert(t);address_space_register_map_client(&as,b,callback,&as);sthread_join(t);hook=0;
  run_ready();assert(callbacks==1&&retro_atomic_load_relaxed_64(&as.bounce_buffer_size)==0);cleanup(&as,b);
 }
}
static void cancellation_cases(void){
 for(unsigned n=0;n<100;n++){
  AddressSpace *as=malloc(sizeof(*as));assert(as);initialize(as);AddressSpaceMapClient node={0};AddressSpaceMapClient*b=&node;callbacks=0;
  retro_atomic_store_relaxed_64(&as->bounce_buffer_size,4);address_space_register_map_client(as,b,callback,as);
  retro_atomic_store_relaxed_int(&go,1);retro_atomic_store_relaxed_int(&done,0);sthread_t*t=sthread_create(release_once,as);assert(t);sthread_join(t);
  address_space_unregister_map_client(as,b);address_space_unregister_map_client(as,b);address_space_cancel_map_client_bh(as);
  free(as);run_ready();assert(!callbacks); /* queued notifier must not dereference freed opaque */
 }
}
static void cancellation_during_release(void){
 AddressSpace as;initialize(&as);AddressSpaceMapClient node={0};AddressSpaceMapClient*b=&node;callbacks=0;
 retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);address_space_register_map_client(&as,b,callback,&as);
 retro_atomic_store_relaxed_int(&go,1);retro_atomic_store_relaxed_int(&done,0);
 pause_release=true;retro_atomic_store_relaxed_int(&release_hit,0);retro_atomic_store_relaxed_int(&release_resume,0);
 sthread_t*t=sthread_create(release_once,&as);assert(t);await(&release_hit,1);
 address_space_unregister_map_client(&as,b);run_ready();assert(!callbacks);
 retro_atomic_store_release_int(&release_resume,1);sthread_join(t);pause_release=false;run_ready();assert(!callbacks);
 address_space_cancel_map_client_bh(&as);run_ready();
}
/* The real DMA consumer must clear its wait before resuming, and cancellation
 * must unlink its caller-owned node before handing control to its callback. */
#include <stddef.h>
#include <errno.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define trace_dma_aio_cancel(...) ((void)0)
typedef struct {void (*cb)(void *,int);void *opaque;} BlockAIOCB;
typedef struct {AddressSpace *as;} SG;
typedef struct {BlockAIOCB common;BlockAIOCB *acb;SG *sg;AddressSpaceMapClient map_client;bool waiting_for_map;} DMAAIOCB;
static unsigned resumed,cancelled;
static void dma_blk_cb(void *opaque,int ret){DMAAIOCB*d=opaque;assert(!ret&&!d->waiting_for_map&&!d->map_client.registered);resumed++;}
static void blk_aio_cancel_async(BlockAIOCB *a){(void)a;abort();}
static void dma_complete(DMAAIOCB*d,int ret){assert(!d->acb&&!d->waiting_for_map&&!d->map_client.registered);d->common.cb(d->common.opaque,ret);}
/* DMA CONSUMER */
static void cancelled_cb(void *opaque,int ret){DMAAIOCB*d=opaque;assert(ret==-ECANCELED&&!d->waiting_for_map&&!d->map_client.registered);cancelled++;}
static void consumer_cases(void){
 AddressSpace as;initialize(&as);SG sg={&as};
 for(unsigned n=0;n<100;n++){
  DMAAIOCB d={.sg=&sg,.waiting_for_map=true};d.common.cb=cancelled_cb;d.common.opaque=&d;
  retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);
  address_space_register_map_client(&as,&d.map_client,reschedule_dma,&d);
  release_resources(&as,4);
  if(n&1){dma_aio_cancel(&d.common);run_ready();}else{run_ready();assert(!d.waiting_for_map);}
 }
 assert(resumed==50&&cancelled==50);address_space_cancel_map_client_bh(&as);run_ready();
}
typedef struct Chain {AddressSpaceMapClient node;AddressSpace *as;struct Chain *victim;unsigned calls;bool again;} Chain;
static bool inside;
static void chained(void *opaque){
 Chain*c=opaque;assert(!inside&&!c->node.registered);inside=true;c->calls++;
 if(c->victim){address_space_unregister_map_client(c->as,&c->victim->node);free(c->victim);c->victim=NULL;}
 if(c->again){c->again=false;address_space_register_map_client(c->as,&c->node,chained,c);assert(c->calls==1);}
 inside=false;
}
static void snapshot_cases(void){
 AddressSpace as;initialize(&as);Chain a={.as=&as,.again=true};Chain*b=calloc(1,sizeof(*b));assert(b);b->as=&as;a.victim=b;
 address_space_register_map_client(&as,&b->node,chained,b);
 address_space_register_map_client(&as,&a.node,chained,&a);
 run_ready();assert(a.calls==2&&!a.node.registered&&QLIST_EMPTY(&as.map_client_list));
 address_space_cancel_map_client_bh(&as);run_ready();
}
enum{CYCLES=20000};
static void stress_release(void*opaque){for(int n=1;n<=CYCLES;n++){await(&go,n);release_resources(opaque,4);retro_atomic_store_release_int(&done,n);}}
int main(void){
 AddressSpace as;initialize(&as);unsigned before=bh_allocs;release_resources(&as,0);assert(bh_allocs==before&&!retro_atomic_load_relaxed_ptr(&as.map_client_bh));
 AddressSpaceMapClient node={0};AddressSpaceMapClient*b=&node;callbacks=0;address_space_register_map_client(&as,b,callback,&as);run_ready();assert(callbacks==1);cleanup(&as,b);
 initialize(&as);node=(AddressSpaceMapClient){0};b=&node;callbacks=0;retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);
 address_space_register_map_client(&as,b,callback,&as);release_resources(&as,4);assert(!callbacks);run_ready();assert(callbacks==1);cleanup(&as,b);
 phase_cases();cancellation_cases();cancellation_during_release();consumer_cases();snapshot_cases();
 initialize(&as);node=(AddressSpaceMapClient){0};b=&node;callbacks=0;retro_atomic_store_relaxed_int(&go,0);retro_atomic_store_relaxed_int(&done,0);
 unsigned warm_allocs=bh_allocs;
 sthread_t*t=sthread_create(stress_release,&as);assert(t);
 for(int n=1;n<=CYCLES;n++){
  retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);
  if(n&1)retro_atomic_store_release_int(&go,n);
  address_space_register_map_client(&as,b,callback,&as);
  if(!(n&1))retro_atomic_store_release_int(&go,n);
  await(&done,n);run_ready();assert(callbacks==(unsigned)n&&QLIST_EMPTY(&as.map_client_list));
 }
 sthread_join(t);assert(bh_allocs==warm_allocs+1);cleanup(&as,b);
 for(unsigned i=0;i<16;i++)assert(!all[i]);
 puts("DMA retry: lazy allocation, owner/foreign release, four forced publication phases, cancellation, queued teardown, DMA resume/cancel, snapshot reuse/free and 20000 register/release races PASS");return 0;
}