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
static QEMUBH *all[16];static unsigned bh_allocs,clients;
static void *allocate(size_t n){clients++;void*p=malloc(n);assert(p);return p;}
static void release(void*p){assert(clients);clients--;free(p);}
#define g_malloc allocate
#define g_free release
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
static void cleanup(AddressSpace*as,QEMUBH*client){
 assert(QLIST_EMPTY(&as->map_client_list));address_space_cancel_map_client_bh(as);qemu_bh_delete(client);run_ready();assert(!clients);
}
static void phase_cases(void){
 for(unsigned phase=1;phase<=4;phase++)for(unsigned n=0;n<100;n++){
  AddressSpace as;initialize(&as);QEMUBH*b=qemu_bh_new(callback,&as);callbacks=0;
  retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);retro_atomic_store_relaxed_int(&go,0);retro_atomic_store_relaxed_int(&done,0);hook=phase;
  sthread_t*t=sthread_create(release_once,&as);assert(t);address_space_register_map_client(&as,b);sthread_join(t);hook=0;
  run_ready();assert(callbacks==1&&retro_atomic_load_relaxed_64(&as.bounce_buffer_size)==0);cleanup(&as,b);
 }
}
static void cancellation_cases(void){
 for(unsigned n=0;n<100;n++){
  AddressSpace *as=malloc(sizeof(*as));assert(as);initialize(as);QEMUBH*b=qemu_bh_new(callback,as);callbacks=0;
  retro_atomic_store_relaxed_64(&as->bounce_buffer_size,4);address_space_register_map_client(as,b);
  retro_atomic_store_relaxed_int(&go,1);retro_atomic_store_relaxed_int(&done,0);sthread_t*t=sthread_create(release_once,as);assert(t);sthread_join(t);
  address_space_unregister_map_client(as,b);address_space_unregister_map_client(as,b);address_space_cancel_map_client_bh(as);qemu_bh_delete(b);
  free(as);run_ready();assert(!callbacks&&!clients); /* queued notifier must not dereference freed opaque */
 }
}
static void cancellation_during_release(void){
 AddressSpace as;initialize(&as);QEMUBH*b=qemu_bh_new(callback,&as);callbacks=0;
 retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);address_space_register_map_client(&as,b);
 retro_atomic_store_relaxed_int(&go,1);retro_atomic_store_relaxed_int(&done,0);
 pause_release=true;retro_atomic_store_relaxed_int(&release_hit,0);retro_atomic_store_relaxed_int(&release_resume,0);
 sthread_t*t=sthread_create(release_once,&as);assert(t);await(&release_hit,1);
 address_space_unregister_map_client(&as,b);qemu_bh_delete(b);run_ready();assert(!clients&&!callbacks);
 retro_atomic_store_release_int(&release_resume,1);sthread_join(t);pause_release=false;run_ready();assert(!callbacks);
 address_space_cancel_map_client_bh(&as);run_ready();
}
enum{CYCLES=20000};
static void stress_release(void*opaque){for(int n=1;n<=CYCLES;n++){await(&go,n);release_resources(opaque,4);retro_atomic_store_release_int(&done,n);}}
int main(void){
 AddressSpace as;initialize(&as);unsigned before=bh_allocs;release_resources(&as,0);assert(bh_allocs==before&&!retro_atomic_load_relaxed_ptr(&as.map_client_bh));
 QEMUBH*b=qemu_bh_new(callback,&as);callbacks=0;address_space_register_map_client(&as,b);run_ready();assert(callbacks==1);cleanup(&as,b);
 initialize(&as);b=qemu_bh_new(callback,&as);callbacks=0;retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);
 address_space_register_map_client(&as,b);release_resources(&as,4);assert(!callbacks);run_ready();assert(callbacks==1);cleanup(&as,b);
 phase_cases();cancellation_cases();cancellation_during_release();
 initialize(&as);b=qemu_bh_new(callback,&as);callbacks=0;retro_atomic_store_relaxed_int(&go,0);retro_atomic_store_relaxed_int(&done,0);
 sthread_t*t=sthread_create(stress_release,&as);assert(t);
 for(int n=1;n<=CYCLES;n++){
  retro_atomic_store_relaxed_64(&as.bounce_buffer_size,4);
  if(n&1)retro_atomic_store_release_int(&go,n);
  address_space_register_map_client(&as,b);
  if(!(n&1))retro_atomic_store_release_int(&go,n);
  await(&done,n);run_ready();assert(callbacks==(unsigned)n&&QLIST_EMPTY(&as.map_client_list));
 }
 sthread_join(t);cleanup(&as,b);
 for(unsigned i=0;i<16;i++)assert(!all[i]);
 puts("DMA retry: lazy allocation, owner/foreign release, four forced publication phases, cancellation, queued teardown and 20000 register/release races PASS");return 0;
}