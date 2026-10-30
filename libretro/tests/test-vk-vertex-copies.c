/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(uint64_t)4095)
#define ROUND_UP(x,a) (((x)+(a)-1)&~((uint64_t)(a)-1))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define BUFFER_VERTEX_INLINE_STAGING 0
#define DIRTY_MEMORY_NV2A 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define NV2A_VK_DPRINTF(...) ((void)0)
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
typedef uint64_t hwaddr,VkDeviceSize,VkDeviceAddress;
typedef struct {hwaddr addr,size;} MemorySyncRequirement;
/* TYPES */
typedef struct {uint8_t *mapped;VkDeviceSize buffer_offset,buffer_size;} StorageBuffer;
typedef struct {
 uint64_t guard1;MemorySyncRequirement vertex_ram_buffer_syncs[16];uint64_t guard2;
 size_t num_vertex_ram_buffer_syncs;MemorySyncRequirement vertex_attr_span[16];
 uint64_t vertex_stale_bitmap[4],vertex_consumed_bitmap[4];
 StorageBuffer storage_buffers[1];VkDeviceSize vertex_attribute_offsets[16];
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;uint8_t *vram_ptr;void *vram;} NV2AState;
static NV2AState d;
static PGRAPHVkState r;
static uint8_t ram[256*4096],staging[65536],gold[65536];
static bool dirty[256],touch[256];
static MemorySyncRequirement expected[16];static unsigned expected_action[16],nexpected,nquery,nbind;static bool expected_dirty[16];
static unsigned action[16],calls;static size_t copied;
static uint32_t seed=0x38b17a5e;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static bool bit(const uint64_t *p,size_t n){return (p[n/64]>>(n%64))&1;}
static size_t find_next_bit(const uint64_t *p,size_t end,size_t start){while(start<end&&!bit(p,start))start++;return start;}
static void bitmap_set(uint64_t *p,size_t start,size_t n){for(size_t i=start;i<start+n;i++)p[i/64]|=UINT64_C(1)<<(i%64);}
static bool memory_region_test_and_clear_dirty(void *mr,hwaddr addr,hwaddr size,int client){
 assert(mr==dirty&&client==0&&nquery<nexpected);assert(addr==expected[nquery].addr&&size==expected[nquery].size);nquery++;
 bool found=false;for(size_t i=addr/4096;i<(addr+size)/4096;i++){found|=dirty[i];dirty[i]=false;}return found;
}
static void pgraph_vk_update_vertex_ram_buffer(PGRAPHState *pg,hwaddr addr,const void *data,VkDeviceSize size){
 assert(pg==&d.pgraph&&data==ram+addr&&size==expected[nquery-1].size);action[nquery-1]=1;
}
static void vertex_cow_snapshot(PGRAPHState *pg,hwaddr addr,VkDeviceSize size,bool is_dirty,VertexCowDispatch *out){
 assert(pg==&d.pgraph&&addr==expected[nquery-1].addr&&size==expected[nquery-1].size);assert(is_dirty==expected_dirty[nquery-1]);action[nquery-1]=2;
 *out=(VertexCowDispatch){true,addr,1,addr*2};
}
static void vertex_cow_bind_attributes(PGRAPHVkState *state,const MemorySyncRequirement *merged,const VertexCowDispatch *dispatch,int n){
 assert(state==&r&&merged==r.vertex_ram_buffer_syncs&&n==(int)nexpected);nbind++;
 for(unsigned i=0;i<nexpected;i++){
  assert(merged[i].addr==expected[i].addr&&merged[i].size==expected[i].size&&action[i]==expected_action[i]);
  if(action[i]==2)assert(dispatch[i].cow&&dispatch[i].lo==merged[i].addr&&dispatch[i].slot==1&&dispatch[i].arena_offset==merged[i].addr*2);
  else assert(!dispatch[i].cow&&!dispatch[i].lo&&!dispatch[i].slot&&!dispatch[i].arena_offset);
 }
}
static bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg,int index,VkDeviceSize size,unsigned align){assert(pg==&d.pgraph&&index==0&&align==256);return r.storage_buffers[0].buffer_offset+size<=sizeof(staging);}
static void *counted_copy(void *out,const void *in,size_t size){calls++;copied+=size;return memcpy(out,in,size);}
#define memcpy counted_copy
/* PRODUCTION */
#undef memcpy
int main(void){
 d=(NV2AState){.pgraph={&r},.vram_ptr=ram,.vram=dirty};for(size_t i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)rnd();
 for(unsigned trial=0;trial<10000;trial++){
  memset(&r,0,sizeof(r));unsigned n=0;
  for(unsigned a=0;a<16;a++){
   size_t addr=1+rnd()%65536,size=(rnd()&1)?0:1+rnd()%65536;
   update_memory_buffer(&d,a,addr,size);
   if(size){assert(r.vertex_ram_buffer_syncs[n].addr==addr&&r.vertex_ram_buffer_syncs[n].size==size);n++;}
   assert(r.num_vertex_ram_buffer_syncs==n&&r.vertex_attr_span[a].addr==addr&&r.vertex_attr_span[a].size==size);
  }
 }
 for(unsigned trial=0;trial<100000;trial++){
  memset(&r,0,sizeof(r));r.guard1=r.guard2=UINT64_C(0x182fe0ac19387e);memset(touch,0,sizeof(touch));memset(action,0,sizeof(action));
  uint64_t initial_consumed[4]={0};bool initial_dirty[256];
  for(unsigned p=0;p<256;p++){dirty[p]=initial_dirty[p]=(rnd()&7)==0;if((rnd()&7)==0)bitmap_set(r.vertex_stale_bitmap,p,1);if((rnd()&7)==0)bitmap_set(r.vertex_consumed_bitmap,p,1);}
  memcpy(initial_consumed,r.vertex_consumed_bitmap,sizeof(initial_consumed));
  unsigned n=trial%17;r.num_vertex_ram_buffer_syncs=n;
  for(unsigned i=0;i<n;i++){
   unsigned addr=rnd()%(sizeof(ram)-65536),size=1+rnd()%65536;
   r.vertex_ram_buffer_syncs[i]=(MemorySyncRequirement){addr,size};
   unsigned first=addr/4096,last=(addr+size+4095)/4096;for(unsigned p=first;p<last;p++)touch[p]=true;
  }
  /* Independent union: walk covered pages, not sorted input intervals. */
  nexpected=0;for(unsigned p=0;p<256;){if(!touch[p]){p++;continue;}unsigned first=p;bool di=false,st=false,co=false;while(p<256&&touch[p]){di|=dirty[p];st|=bit(r.vertex_stale_bitmap,p);co|=bit(r.vertex_consumed_bitmap,p);p++;}expected[nexpected]=(MemorySyncRequirement){first*4096,(p-first)*4096};expected_dirty[nexpected]=di;expected_action[nexpected++]=(di||st)?(co?2:1):0;}
  nquery=nbind=0;sync_vertex_ram_buffer(&d.pgraph);assert(!r.num_vertex_ram_buffer_syncs&&nquery==nexpected&&nbind==(n?1u:0u));
  for(unsigned p=0;p<256;p++){assert(bit(r.vertex_consumed_bitmap,p)==(touch[p]||bit(initial_consumed,p)));assert(dirty[p]==(touch[p]?false:initial_dirty[p]));}
  assert(r.guard1==UINT64_C(0x182fe0ac19387e)&&r.guard2==r.guard1);
 }
 static const unsigned widths[]={1,2,3,4,6,8,12,16};uint64_t old_calls=0,new_calls=0,total_bytes=0;
 for(unsigned trial=0;trial<20000;trial++){
  memset(&r,0,sizeof(r));memset(staging,0xcc,sizeof(staging));memset(gold,0xcc,sizeof(gold));unsigned count=trial%129,nattrs=1+rnd()%16;
  size_t begin=rnd()%64,out=0;r.storage_buffers[0]=(StorageBuffer){staging,begin,sizeof(staging)};VertexBufferRemap remap={0};unsigned want_calls=0;size_t want_bytes=0;
  for(unsigned a=0;a<nattrs;a++){
   size_t width=widths[rnd()%8],stride=(rnd()&1)?width:rnd()%65,base=a*16384+1+rnd()%63;
   r.vertex_attribute_offsets[a]=base;remap.attributes|=1u<<a;remap.map[a].offset=out;remap.map[a].new_stride=width;remap.map[a].old_stride=stride;
   for(unsigned v=0;v<count;v++)for(size_t b=0;b<width;b++)gold[begin+out+v*width+b]=ram[base+v*stride+b];
   old_calls+=count;want_calls+=stride==width?(count?1:0):count;want_bytes+=width*count;out+=width*count+rnd()%8;
  }
  remap.buffer_space_required=out;calls=copied=0;copy_remapped_attributes_to_inline_buffer(&d.pgraph,remap,0,count);
  assert(!memcmp(staging,gold,sizeof(staging))&&r.storage_buffers[0].buffer_offset==begin+out&&calls==want_calls&&copied==want_bytes);
  for(unsigned a=0;a<nattrs;a++)assert(r.vertex_attribute_offsets[a]==begin+remap.map[a].offset);
  new_calls+=calls;total_bytes+=copied;
 }
 printf("PASS: 10000 zero/nonzero sync-queue cases; 100000 independent page-union/dirty/COW cases; 20000 full-byte remap/redzone cases; copy calls %llu -> %llu, %llu bytes preserved; UB traps enabled\n",(unsigned long long)old_calls,(unsigned long long)new_calls,(unsigned long long)total_bytes);
}
