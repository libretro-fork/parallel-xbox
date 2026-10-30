/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define TARGET_PAGE_MASK (~UINT64_C(4095))
#define TARGET_PAGE_ALIGN(a) (((a)+4095)&TARGET_PAGE_MASK)
#define GL_ARRAY_BUFFER 1
#define DIRTY_MEMORY_NV2A 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 0
#define VRAM (64u*1024*1024)
typedef uint64_t hwaddr;
typedef struct {unsigned gl_memory_buffer;} PGRAPHGLState;
typedef struct {hwaddr addr,size;} Span;
typedef Span MemorySyncRequirement;
typedef struct {size_t num_vertex_ram_buffer_syncs;Span vertex_ram_buffer_syncs[16],vertex_attr_span[16];} PGRAPHVkState;
typedef struct {PGRAPHGLState *gl_renderer_state;PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;uint8_t *vram_ptr;void *vram;} NV2AState;
static uint8_t ram[VRAM];static bool dirty;static unsigned queries,uploads,binds;static hwaddr queried_addr,queried_size;
static PGRAPHGLState gl={42};static PGRAPHVkState vk;static NV2AState machine={{&gl,&vk},ram,&dirty};
static void glBindBuffer(unsigned target,unsigned id){assert(target==GL_ARRAY_BUFFER&&id==42);binds++;}
static hwaddr memory_region_size(void *mr){assert(mr==&dirty);return VRAM;}
static bool memory_region_test_and_clear_dirty(void *mr,hwaddr addr,hwaddr size,int client){assert(mr==&dirty&&client==0&&size&&addr+size<=VRAM);queries++;queried_addr=addr;queried_size=size;bool old=dirty;dirty=false;return old;}
static void glBufferSubData(unsigned target,hwaddr addr,hwaddr size,const void *data){assert(target==GL_ARRAY_BUFFER&&addr==queried_addr&&size==queried_size&&data==ram+addr);uploads++;}
static void nv2a_profile_inc_counter(unsigned counter){assert(counter==0);}
/* HELPER */
static hwaddr last_addr,last_end; /* gl/vertex.c's, at file scope there */
/* UPDATERS */
static bool gl_bind_range(hwaddr attrib_data_addr,uint32_t min_element,uint32_t num_elements,size_t stride,size_t element_size,bool updated_memory_buffer){
 NV2AState *d=&machine;hwaddr start;
 /* GL BLOCK */
 return updated_memory_buffer;
}
static void vk_bind_range(hwaddr attrib_data_addr,uint32_t min_element,uint32_t num_elements,size_t stride,size_t element_size){
 NV2AState *d=&machine;hwaddr start;int i=0;
 /* VK BLOCK */
}
static uint32_t seed=0x1790ad91;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint64_t oracle(unsigned count,unsigned stride,unsigned width){
 if(!stride)return 0;
 uint64_t end=0;
 for(unsigned vertex=0;vertex<count;vertex++)for(unsigned component=0;component<width;component++){
  uint64_t byte=(uint64_t)vertex*stride+component;end=MAX(end,byte+1);
 }
 return end;
}
static void check_range(hwaddr base,unsigned min,unsigned count,unsigned stride,unsigned width){
 uint64_t want=oracle(count,stride,width),start=base+(uint64_t)min*stride;
 assert(pgraph_vertex_fetch_span(count,stride,width)==want);vk.num_vertex_ram_buffer_syncs=0;vk_bind_range(base,min,count,stride,width);
 assert(vk.num_vertex_ram_buffer_syncs==(want?1u:0u)&&vk.vertex_attr_span[0].addr==start&&vk.vertex_attr_span[0].size==want);
 if(want)assert(vk.vertex_ram_buffer_syncs[0].addr==start&&vk.vertex_ram_buffer_syncs[0].size==want);
 dirty=true;queries=uploads=binds=0;bool updated=gl_bind_range(base,min,count,stride,width,false);
 assert(updated==(want!=0)&&queries==(want?1u:0u)&&uploads==queries&&binds==queries);
 if(want)assert(queried_addr==(start&TARGET_PAGE_MASK)&&queried_size==TARGET_PAGE_ALIGN(start+want)-(start&TARGET_PAGE_MASK));
}
int main(void){
 for(unsigned n=0;n<200000;n++){
  unsigned count=rnd()%129,stride=rnd()%256,width=1+rnd()%16;assert(pgraph_vertex_fetch_span(count,stride,width)==oracle(count,stride,width));
  if(n<2000)check_range(rnd()%65536,rnd()%512,count,stride,width);
 }
 check_range(VRAM-16,0,1,16,16); /* final VRAM byte is a legal fetch */
 check_range(4096-12,0,1,32,12);assert(queried_size==4096); /* padding must not dirty/upload the following page */
 check_range(4096-8,0,2,4,8);assert(queried_size==8192); /* overlap extends past count*stride */
 check_range(13,0,0,32,16);check_range(13,0,100,0,16);
 assert(pgraph_vertex_fetch_span(UINT32_MAX,255,16)==UINT64_C(1095216659986));
 /* An empty uniform attribute must not turn on the quick-cache flag before
  * this draw's first real update, even if the prior draw used that range. */
 dirty=true;(void)gl_bind_range(8192,0,1,16,16,false);
 dirty=true;queries=uploads=binds=0;bool updated=gl_bind_range(31,0,200,0,16,false);assert(!updated&&!queries&&!binds);
 updated=gl_bind_range(8192,0,1,16,16,updated);assert(updated&&queries==1&&uploads==1);
 /* A later real attribute in the same covered page still uses quick reuse. */
 (void)gl_bind_range(8200,0,1,16,4,updated);assert(queries==1&&uploads==1);
 puts("PASS: 200000 independent byte-walk spans, 2005 production GL/VK range cases, empty/uniform quick-cache sequencing, overlap, page/VRAM ends and 64-bit products; UB traps enabled");
}
