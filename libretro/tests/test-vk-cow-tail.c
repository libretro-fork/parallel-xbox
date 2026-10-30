/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ROUND_UP(x,a) (((x)+(a)-1)&~((uint64_t)(a)-1))
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~UINT64_C(4095))
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 0
#define DIRTY_MEMORY_NV2A 0
#define VK_FINISH_REASON_NEED_BUFFER_SPACE 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define RAMSIZE 131072u
#define ARENASIZE 65536u
typedef uint64_t hwaddr,VkDeviceSize;
/* TYPES */
typedef struct {uint8_t *mapped;VkDeviceSize buffer_offset,buffer_size;} StorageBuffer;
typedef struct {hwaddr addr,size;} Span;
typedef struct {
 StorageBuffer vertex_arena[2];bool vertex_arena_coherent[2],vertex_arena_pending,vertex_arena_used;
 Span vertex_attr_span[16];int vertex_attribute_to_description_location[16];hwaddr vertex_attribute_offsets[16];
 VertexCowCacheEntry vertex_cow_cache[512];unsigned vertex_cow_recent[4],vertex_cow_tail[2],vertex_cow_cache_rr;uint32_t vertex_arena_epoch[2];unsigned slot;
 struct{struct{uint64_t nonCoherentAtomSize;} limits;} device_props;
 uint64_t vertex_stale_bitmap[1];
} PGRAPHVkState;
typedef struct{PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct{PGRAPHState pgraph;uint8_t *vram_ptr;void *vram;} NV2AState;
static bool late_dirty;static PGRAPHVkState r;static NV2AState d={{&r},NULL,&late_dirty};static uint8_t ram[RAMSIZE],arena[2][ARENASIZE],mirror[RAMSIZE];
static struct {const uint8_t *data;size_t size;uint8_t expected[ARENASIZE];} readers[32];static unsigned nreaders;
static unsigned copies,finishes,settles,downloads;static size_t copied;static bool rotate_download,writeback_download,cpu_write_download;
static uint32_t seed=0x1f09a28b;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void check_readers(void){for(unsigned i=0;i<nreaders;i++)assert(!memcmp(readers[i].data,readers[i].expected,readers[i].size));}
static void pgraph_vk_vertex_cow_cache_invalidate(PGRAPHVkState *,hwaddr,VkDeviceSize);
static void pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,hwaddr lo,VkDeviceSize size){
 assert(pg==&d.pgraph&&lo+size<=RAMSIZE);downloads++;
 if(rotate_download){r.slot^=1;rotate_download=false;}
 if(cpu_write_download){ram[lo+4096]^=0x73;late_dirty=true;cpu_write_download=false;}
 if(writeback_download){ram[lo]^=0xa7;pgraph_vk_vertex_cow_cache_invalidate(&r,lo,1);writeback_download=false;}
}
static bool memory_region_test_and_clear_dirty(void *mr,hwaddr addr,hwaddr size,unsigned client){assert(mr==&late_dirty&&addr+size<=RAMSIZE&&size&&client==0);bool found=late_dirty;late_dirty=false;return found;}
static void pgraph_vk_finish(PGRAPHState *pg,int reason){assert(pg==&d.pgraph&&reason==0);check_readers();finishes++;r.vertex_arena_used=false;}
static void pgraph_vk_settle_all_slots(PGRAPHState *pg){assert(pg==&d.pgraph);check_readers();nreaders=0;settles++;}
static void pgraph_vk_update_vertex_ram_buffer(PGRAPHState *pg,hwaddr addr,void *data,VkDeviceSize size){assert(pg==&d.pgraph&&data==ram+addr&&addr+size<=RAMSIZE);memcpy(mirror+addr,data,size);}
static void bitmap_set(uint64_t *bits,size_t start,size_t count){assert(start+count<=RAMSIZE/4096);for(size_t i=start;i<start+count;i++)*bits|=UINT64_C(1)<<i;}
static void nv2a_profile_inc_counter(unsigned n){assert(n==0);}
static void *counted_copy(void *out,const void *in,size_t len){copies++;copied+=len;return memcpy(out,in,len);}
#define memcpy counted_copy
/* PRODUCTION */
#undef memcpy
static void reset(void){
 check_readers();nreaders=0;memset(&r,0,sizeof(r));memset(arena,0xcd,sizeof(arena));memset(mirror,0xcc,sizeof(mirror));
 r.device_props.limits.nonCoherentAtomSize=256;for(unsigned s=0;s<2;s++){r.vertex_arena[s]=(StorageBuffer){arena[s],0,ARENASIZE};r.vertex_arena_coherent[s]=true;}
 for(unsigned a=0;a<16;a++)r.vertex_attribute_to_description_location[a]=-1;
 for(size_t i=0;i<RAMSIZE;i++)ram[i]=(uint8_t)rnd();
 d.vram_ptr=ram;copies=copied=finishes=settles=downloads=0;rotate_download=writeback_download=cpu_write_download=late_dirty=false;
}
static VertexCowDispatch snapshot(hwaddr addr,VkDeviceSize size,hwaddr lo,bool dirty){
 r.vertex_attr_span[0]=(Span){addr,size};r.vertex_attribute_to_description_location[0]=0;r.vertex_attribute_offsets[0]=lo;
 VertexCowDispatch out={0};vertex_cow_snapshot(&d.pgraph,addr,size,dirty,&out);check_readers();
 if(out.cow){const uint8_t *p=r.vertex_arena[out.slot].mapped+out.arena_offset;assert(out.lo<=addr&&addr+size-out.lo<=ARENASIZE);assert(!memcmp(p+(addr-out.lo),ram+addr,size));}
 else assert(!memcmp(mirror+addr,ram+addr,size));
 return out;
}
static void hold(VertexCowDispatch out,hwaddr end){
 if(!out.cow)return;
 assert(nreaders<ARRAY_SIZE(readers));size_t size=end-out.lo;readers[nreaders].data=r.vertex_arena[out.slot].mapped+out.arena_offset;readers[nreaders].size=size;memcpy(readers[nreaders].expected,readers[nreaders].data,size);nreaders++;r.vertex_arena_pending=false;r.vertex_arena_used=true;
}
int main(void){
 reset();VertexCowDispatch a=snapshot(4096,4096,0,true);hold(a,8192);size_t before=copied;uint64_t stale=*r.vertex_stale_bitmap;
 VertexCowDispatch b=snapshot(4096,8192,0,false);assert(b.arena_offset==a.arena_offset&&b.slot==a.slot&&copied-before==4096&&*r.vertex_stale_bitmap==stale);hold(b,12288);
 before=copied;VertexCowDispatch c=snapshot(4096,16384,0,false);assert(c.arena_offset==a.arena_offset&&copied-before==8192);hold(c,20480);
 before=copied;(void)snapshot(8192,4096,0,false);assert(copied==before); /* contained reuse */
 ram[4096]^=0x19;before=copied;VertexCowDispatch changed=snapshot(4096,16384,0,true);assert(changed.arena_offset!=a.arena_offset&&copied-before==20480);hold(changed,20480);
 /* Dirt in an artificial binding prefix outside this draw's fetches
  * must stay pending, not force a read or be cleared by revalidation. */
 reset();a=snapshot(4096,4096,0,true);hold(a,8192);ram[100]^=0x61;late_dirty=true;before=copied;b=snapshot(8192,4096,0,false);assert(b.arena_offset==a.arena_offset&&copied-before==4096&&late_dirty);hold(b,12288);
 /* The non-coherent boundary is enforced even when the entry otherwise fits. */
 reset();r.vertex_arena_coherent[0]=false;a=snapshot(4096,4096,13,true);hold(a,8192);before=copied;b=snapshot(4096,8192,13,false);assert(b.arena_offset!=a.arena_offset&&copied-before==12288-13);
 reset();r.vertex_arena_coherent[0]=false;a=snapshot(4096,4096,0,true);hold(a,8192);before=copied;b=snapshot(4096,8192,0,false);assert(b.arena_offset==a.arena_offset&&copied-before==4096);
 /* Download callbacks may invalidate the prefix or rotate to another slot. */
 reset();a=snapshot(4096,4096,0,true);hold(a,8192);writeback_download=true;before=copied;b=snapshot(4096,8192,0,false);assert(b.arena_offset!=a.arena_offset&&copied-before==12288);
 reset();a=snapshot(4096,4096,0,true);hold(a,8192);cpu_write_download=true;before=copied;b=snapshot(4096,8192,0,false);assert(b.arena_offset!=a.arena_offset&&copied-before==12288&&(*r.vertex_stale_bitmap&(UINT64_C(1)<<2)));
 reset();a=snapshot(4096,4096,0,true);hold(a,8192);rotate_download=true;before=copied;b=snapshot(4096,8192,0,false);assert(b.slot==1&&copied-before==12288);
 /* Pending snapshots cannot be recycled merely because earlier GPU work
  * settled: this draw has not bound them yet. */
 reset();r.vertex_arena[0].buffer_size=r.vertex_arena[1].buffer_size=8192;
 a=snapshot(0,4096,0,true);b=snapshot(8192,4096,8192,true);assert(r.vertex_arena_pending);
 VertexCowDispatch fallback=snapshot(16384,4096,16384,true);assert(!fallback.cow&&r.vertex_arena[0].buffer_offset==8192&&!r.vertex_arena_epoch[0]);
 assert(!memcmp(arena[0]+a.arena_offset,ram,4096)&&!memcmp(arena[0]+b.arena_offset,ram+8192,4096));
 hold(a,4096);hold(b,12288);pgraph_vk_finish(&d.pgraph,0);pgraph_vk_settle_all_slots(&d.pgraph);
 (void)snapshot(32768,4096,32768,true);assert(r.vertex_arena_epoch[0]==1&&r.vertex_cow_tail[0]);
 reset();r.vertex_arena[0].buffer_size=4096;fallback=snapshot(4096,8192,0,true);assert(!fallback.cow&&finishes&&settles);
 reset();r.vertex_cow_tail[0]=513;assert(!vertex_cow_extendable(&r,0,4096));r.vertex_cow_tail[0]=0;assert(!vertex_cow_extendable(&r,0,4096));
 /* Selector adversaries: stale epochs, overwritten entries, wrong base/slot,
  * allocation holes, capacity, and atom boundaries. No array scan is needed. */
 for(unsigned n=0;n<100000;n++){
  memset(&r,0,sizeof(r));unsigned slot=rnd()%2,key=1+rnd()%512;r.slot=slot;r.vertex_cow_tail[slot]=key;r.vertex_arena_coherent[slot]=(rnd()&1)!=0;
  r.device_props.limits.nonCoherentAtomSize=1u<<(rnd()%11);r.vertex_arena[slot].buffer_size=ARENASIZE;
  VertexCowCacheEntry *e=&r.vertex_cow_cache[key-1];e->valid=(rnd()&7)!=0;e->slot=(rnd()&7)?slot:slot^1;e->epoch=rnd()%3;r.vertex_arena_epoch[slot]=rnd()%3;e->addr=13+rnd()%3;e->arena_offset=rnd()%1024;e->size=1+rnd()%10000;
  r.vertex_arena[slot].buffer_offset=e->arena_offset+e->size+((rnd()&7)?0:1);VkDeviceSize want=1+rnd()%70000;hwaddr lo=13+rnd()%3;
  bool eligible=e->valid&&e->slot==slot&&e->epoch==r.vertex_arena_epoch[slot]&&e->addr==lo&&want>e->size&&e->arena_offset+e->size==r.vertex_arena[slot].buffer_offset&&e->arena_offset+want<=ARENASIZE&&(r.vertex_arena_coherent[slot]||r.vertex_arena[slot].buffer_offset%r.device_props.limits.nonCoherentAtomSize==0);
  assert(vertex_cow_extendable(&r,lo,want)==(eligible?e:NULL));
 }
 reset();
 for(unsigned n=0;n<10000;n++){
  if(nreaders==ARRAY_SIZE(readers)){check_readers();nreaders=0;r.vertex_arena_used=false;}
  if(n%97==0)r.slot^=1;
  hwaddr addr=(1+rnd()%12)*4096;VkDeviceSize size=(1+rnd()%4)*4096;hwaddr lo=addr-(rnd()%2?0:4096);
  bool dirty=(rnd()&3)==0;if(dirty)ram[addr+rnd()%size]^=0x5d;
  VertexCowDispatch out=snapshot(addr,size,lo,dirty);hold(out,addr+size);
 }
 check_readers();assert(finishes&&settles&&r.vertex_arena_epoch[0]);printf("PASS: immutable reader snapshots, tail-only growth, dirty/writeback/rotation fallbacks, coherent/atom-aligned gates, 100000 selector cases and 10000 live snapshot cases; UB traps enabled\n");
}
