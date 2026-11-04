/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NV2A_MAX_BATCH_LENGTH 64u
#define BUFFER_VERTEX_INLINE_STAGING 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_3 3
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef uint64_t VkDeviceSize;
typedef uint64_t VkDeviceAddress;
typedef struct {uint8_t *mapped;VkDeviceSize buffer_offset,buffer_size;} StorageBuffer;
typedef struct {bool in_command_buffer,in_draw,vertex_conversion_required;StorageBuffer storage_buffers[1];} PGRAPHVkState;
typedef struct {
 uint32_t inline_array[NV2A_MAX_BATCH_LENGTH];unsigned inline_array_length;
 uint32_t *inline_array_external;bool inline_array_external_committed;
 unsigned inline_elements_length,inline_elements_min,inline_elements_max,inline_buffer_length;
 bool two_side_light_en;PGRAPHVkState *vk_renderer_state;
} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
typedef struct {struct {uint32_t *(*inline_array_begin)(NV2AState*);} ops;} PGRAPHRenderer;
static PGRAPHRenderer renderer;
static const PGRAPHRenderer *pgraph_renderer(PGRAPHState *pg){(void)pg;return &renderer;}
static void pgraph_reset_draw_arrays(PGRAPHState *pg){(void)pg;}
static void pgraph_check_within_begin_end_block(PGRAPHState *pg){(void)pg;}
static unsigned locks,updates,copy_calls;static size_t copy_bytes;
static void nv2a_lock_fifo(NV2AState *d){(void)d;++locks;}
static void nv2a_profile_inc_counter(unsigned n){assert(n==3);++updates;}
static void *tracked_copy(void *dst,const void *src,size_t size)
{++copy_calls;copy_bytes+=size;return memcpy(dst,src,size);}
#define memcpy tracked_copy
/* PRODUCTION */
#undef memcpy
static uint32_t ring_words[4*NV2A_MAX_BATCH_LENGTH];
static NV2AState d;static PGRAPHVkState r;
static void reset(void)
{
 memset(&d,0,sizeof(d));memset(&r,0,sizeof(r));memset(ring_words,0xa5,sizeof(ring_words));
 d.pgraph.vk_renderer_state=&r;r.in_command_buffer=true;
 r.storage_buffers[0]=(StorageBuffer){(uint8_t*)ring_words,0,sizeof(ring_words)};
 renderer.ops.inline_array_begin=pgraph_vk_inline_array_begin;
 copy_calls=updates=0;copy_bytes=0;
}
static void packet(const uint32_t *words,size_t n,bool inc)
{
 size_t consumed=0;
 method_INLINE_ARRAY(&d,&d.pgraph,0,0,words[0],(uint32_t*)words,n,&consumed,inc);
 if(!inc)assert(consumed==n);
}
int main(void)
{
 uint32_t words[NV2A_MAX_BATCH_LENGTH+4];for(unsigned i=0;i<NV2A_MAX_BATCH_LENGTH+4;i++)words[i]=0x43210000u+i;
 for(unsigned reject=0;reject<7;reject++) {
  reset();StorageBuffer *b=&r.storage_buffers[0];
  if(reject==0)r.in_command_buffer=false;
  if(reject==1)r.in_draw=true;
  if(reject==2)b->mapped=NULL;
  if(reject==3)b->buffer_offset=1;
  if(reject==4)b->buffer_offset=sizeof(ring_words)-sizeof(d.pgraph.inline_array)+4;
  if(reject==5)renderer.ops.inline_array_begin=NULL;
  if(reject==6)r.vertex_conversion_required=true;
  VkDeviceSize before=b->buffer_offset,out=UINT64_MAX;
  packet(words,5,false);
  assert(!d.pgraph.inline_array_external&&!memcmp(d.pgraph.inline_array,words,20));
  assert(!pgraph_vk_commit_inline_array(&d.pgraph,20,&out)&&out==UINT64_MAX);
  assert(b->buffer_offset==before&&copy_bytes==20&&!updates);
 }
 for(unsigned batch=0;batch<1000;batch++) {
  reset();StorageBuffer *b=&r.storage_buffers[0];b->buffer_offset=(batch%8)*4;
  VkDeviceSize start=b->buffer_offset,out=UINT64_MAX;
  memset(d.pgraph.inline_array,0x5a,sizeof(d.pgraph.inline_array));
  unsigned n=1+batch%(NV2A_MAX_BATCH_LENGTH-1),at=0;
  while(at<n){unsigned amount=1+(at*7+batch)%9;if(amount>n-at)amount=n-at;bool inc=amount==1;packet(words+at,amount,inc);at+=amount;}
  assert(d.pgraph.inline_array_external==(uint32_t*)(b->mapped+start));
  assert(b->buffer_offset==start&&copy_bytes==n*4);
  assert(d.pgraph.inline_array[0]==0x5a5a5a5a);
  assert(!memcmp(pgraph_inline_array_data(&d.pgraph),words,n*4));
  size_t copies=copy_bytes;
  assert(pgraph_vk_commit_inline_array(&d.pgraph,n*4,&out)&&out==start);
  assert(b->buffer_offset==start+n*4&&copy_bytes==copies&&updates==1);
  assert(pgraph_vk_commit_inline_array(&d.pgraph,n*4,&out)&&updates==1);
  assert(b->buffer_offset==start+n*4);
  /* A later packet cannot modify an already bound upload view. */
  packet(words+n,1,true);
  assert(!d.pgraph.inline_array_external&&!d.pgraph.inline_array_external_committed);
  assert(!memcmp(d.pgraph.inline_array,words,(n+1)*4));
  assert(!memcmp(b->mapped+start,words,n*4));
  assert(b->buffer_offset==start+n*4);
 }
 reset();packet(words,13,false);
 uint32_t snapshot[NV2A_MAX_BATCH_LENGTH];
 pgraph_materialize_inline_array(&d.pgraph); /* owner save/submission boundary */
 assert(!d.pgraph.inline_array_external&&r.storage_buffers[0].buffer_offset==0);
 memcpy(snapshot,d.pgraph.inline_array,13*4);
 memset(ring_words,0xff,sizeof(ring_words));
 packet(words+13,7,false);assert(!memcmp(d.pgraph.inline_array,words,20*4));
 /* Restore over a live external view: incoming CPU bytes must win. */
 pgraph_reset_inline_buffers(&d.pgraph);packet(words+3,5,false);assert(d.pgraph.inline_array_external);
 assert(!nv2a_pre_load(&d)&&locks==1&&!d.pgraph.inline_array_external&&!d.pgraph.inline_array_external_committed);
 memcpy(d.pgraph.inline_array,snapshot,13*4);d.pgraph.inline_array_length=13;
 packet(words+13,7,false);assert(!memcmp(d.pgraph.inline_array,words,20*4));
 VkDeviceSize out;assert(!pgraph_vk_commit_inline_array(&d.pgraph,80,&out));
 /* Conflicting upload first materializes the pending primitive. */
 reset();packet(words,9,false);
 void *data=words+20;VkDeviceSize size=16;
 assert(pgraph_vk_append_to_buffer(&d.pgraph,0,&data,&size,1,1)==0);
 assert(!d.pgraph.inline_array_external&&!memcmp(d.pgraph.inline_array,words,36));
 assert(!memcmp(ring_words,words+20,16));
 /* Reset/cancel drops an uncommitted reservation without publishing bytes. */
 reset();packet(words,7,false);pgraph_reset_inline_buffers(&d.pgraph);
 assert(!d.pgraph.inline_array_external&&!d.pgraph.inline_array_external_committed&&!d.pgraph.inline_array_length);
 assert(!r.storage_buffers[0].buffer_offset);
 packet(words,64,false);assert(d.pgraph.inline_array_length==64);
 assert(pgraph_vk_commit_inline_array(&d.pgraph,256,&out)&&out==0);
 puts("Inline upload writer: 1000 chunked batches, eligibility, no-copy commit/reuse, append-after-bind, snapshot/restore, conflict and cancellation PASS");
 return 0;
}
