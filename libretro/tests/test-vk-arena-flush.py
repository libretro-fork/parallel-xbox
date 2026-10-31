# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check production vertex mirror/arena writes, flush ranges and visibility barriers."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();a=s.index('static void flush_memory_buffer(');b=s.index('{',a)+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
v=(r/'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text();va=v.index('void pgraph_vk_update_vertex_ram_buffer(');vb=v.index('{',va)+1;depth=1
while depth:depth+=(v[vb]=='{')-(v[vb]=='}');vb+=1
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();range_type=re.search(r'typedef struct MemorySyncRequirement \{.*?\} MemorySyncRequirement;',h,re.S).group(0)
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define BUFFER_VERTEX_RAM 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_ALIGN(v) (((v)+4095)&~UINT64_C(4095))
#define VK_FINISH_REASON_VERTEX_BUFFER_DIRTY 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 0
typedef uint64_t hwaddr;
/* RANGE TYPE */
typedef struct {VkBuffer buffer;uintptr_t allocation;VkDeviceSize buffer_offset;unsigned char *mapped;} StorageBuffer;
typedef struct {void *allocator;StorageBuffer storage_buffers[1],vertex_arena[2];bool vertex_arena_coherent[2];VkDeviceSize vertex_arena_dirty_start[2];bool vertex_ram_coherent;MemorySyncRequirement vertex_ram_dirty;uint64_t *vertex_consumed_bitmap,*vertex_stale_bitmap;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static unsigned calls[3],barriers;static VkDeviceSize offsets[3],sizes[3];
static VkResult vmaFlushAllocation(void *allocator,uintptr_t allocation,VkDeviceSize offset,VkDeviceSize size){assert(allocator==(void*)(uintptr_t)7&&allocation>=1&&allocation<=3);unsigned i=allocation-1;calls[i]++;offsets[i]=offset;sizes[i]=size;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier *memory,uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images){assert(cmd==(VkCommandBuffer)(uintptr_t)4&&src==VK_PIPELINE_STAGE_HOST_BIT&&dst==VK_PIPELINE_STAGE_VERTEX_INPUT_BIT&&!flags&&!memory_count&&!memory&&buffer_count==3&&!image_count&&!images);for(unsigned i=0;i<3;i++){assert(buffers[i].sType==VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER&&buffers[i].srcAccessMask==VK_ACCESS_HOST_WRITE_BIT&&buffers[i].dstAccessMask==VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT&&buffers[i].buffer==(VkBuffer)(uintptr_t)(10+i)&&buffers[i].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&buffers[i].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&!buffers[i].offset&&buffers[i].size==VK_WHOLE_SIZE);}barriers++;}
static bool download_flush;
static unsigned finishes,settles,invalidations;
static void flush_memory_buffer(PGRAPHState *,VkCommandBuffer);
static void pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,hwaddr offset,VkDeviceSize size){assert(offset+size<=16384);if(download_flush){download_flush=false;flush_memory_buffer(pg,(VkCommandBuffer)(uintptr_t)4);}}
static size_t find_next_bit(const uint64_t *bits,size_t end,size_t start){for(size_t i=start;i<end;i++)if(*bits&(UINT64_C(1)<<i))return i;return end;}
static void bitmap_clear(uint64_t *bits,size_t start,size_t count){assert(start+count<=4);for(size_t i=start;i<start+count;i++)*bits&=~(UINT64_C(1)<<i);}
static void pgraph_vk_finish(PGRAPHState *pg,unsigned reason){assert(reason==VK_FINISH_REASON_VERTEX_BUFFER_DIRTY);flush_memory_buffer(pg,(VkCommandBuffer)(uintptr_t)4);finishes++;}
static void pgraph_vk_settle_all_slots(PGRAPHState *pg){*pg->vk_renderer_state->vertex_consumed_bitmap=0;settles++;}
static void nv2a_profile_inc_counter(unsigned counter){assert(counter==NV2A_PROF_GEOM_BUFFER_UPDATE_1);}
static void pgraph_vk_vertex_cow_cache_invalidate(PGRAPHVkState *r,hwaddr offset,VkDeviceSize size){(void)r;assert(offset+size<=16384);invalidations++;}
'''
suffix=r'''
int main(void){quiet_test_errors();PGRAPHVkState r={.allocator=(void*)(uintptr_t)7,.storage_buffers={{(VkBuffer)(uintptr_t)10,1,0,NULL}},.vertex_arena={{(VkBuffer)(uintptr_t)11,2,4096,NULL},{(VkBuffer)(uintptr_t)12,3,6000,NULL}},.vertex_arena_coherent={false,true},.vertex_arena_dirty_start={UINT64_MAX,UINT64_MAX}};PGRAPHState pg={&r};VkCommandBuffer cmd=(VkCommandBuffer)(uintptr_t)4;
 flush_memory_buffer(&pg,cmd);assert(!calls[0]&&!calls[1]&&!calls[2]&&barriers==1);
 r.vertex_arena_dirty_start[0]=1024;r.vertex_arena_dirty_start[1]=0;flush_memory_buffer(&pg,cmd);assert(calls[1]==1&&offsets[1]==1024&&sizes[1]==3072&&!calls[2]&&r.vertex_arena_dirty_start[0]==UINT64_MAX&&r.vertex_arena_dirty_start[1]==UINT64_MAX);
 flush_memory_buffer(&pg,cmd);assert(calls[1]==1&&!calls[2]);
 r.vertex_arena[0].buffer_offset=5000;r.vertex_arena_dirty_start[0]=4096;flush_memory_buffer(&pg,cmd);assert(calls[1]==2&&offsets[1]==4096&&sizes[1]==904);
 r.vertex_arena[1].buffer_offset=0;r.vertex_arena_dirty_start[1]=UINT64_MAX;r.vertex_arena_coherent[1]=false;flush_memory_buffer(&pg,cmd);assert(!calls[2]);
 r.vertex_arena[1].buffer_offset=100;r.vertex_arena_dirty_start[1]=0;flush_memory_buffer(&pg,cmd);assert(calls[2]==1&&offsets[2]==0&&sizes[2]==100);
 assert(!calls[0]&&barriers==6);puts("Vertex flush: clean/coherent arenas skip allocator calls; non-coherent dirty tails and recycled arenas use exact byte ranges; all mirror/arena visibility barriers preserved");
 unsigned char mirror[16384]={0},source[16384];for(unsigned i=0;i<sizeof(source);i++)source[i]=(unsigned char)(i*37+11);
 uint64_t consumed=0,stale=15;r.storage_buffers[0].mapped=mirror;r.vertex_consumed_bitmap=&consumed;r.vertex_stale_bitmap=&stale;
 pgraph_vk_update_vertex_ram_buffer(&pg,1024,source+1024,128);pgraph_vk_update_vertex_ram_buffer(&pg,4096,source+4096,64);pgraph_vk_update_vertex_ram_buffer(&pg,800,source+800,32);
 assert(r.vertex_ram_dirty.addr==800&&r.vertex_ram_dirty.size==3360&&!calls[0]);assert(!memcmp(mirror+1024,source+1024,128)&&!memcmp(mirror+4096,source+4096,64)&&!memcmp(mirror+800,source+800,32));flush_memory_buffer(&pg,cmd);assert(calls[0]==1&&offsets[0]==800&&sizes[0]==3360&&!r.vertex_ram_dirty.size);flush_memory_buffer(&pg,cmd);assert(calls[0]==1);
 pgraph_vk_update_vertex_ram_buffer(&pg,200,source+200,32);download_flush=true;pgraph_vk_update_vertex_ram_buffer(&pg,8000,source+8000,100);assert(calls[0]==2&&offsets[0]==200&&sizes[0]==32&&r.vertex_ram_dirty.addr==8000&&r.vertex_ram_dirty.size==100);flush_memory_buffer(&pg,cmd);assert(calls[0]==3&&offsets[0]==8000&&sizes[0]==100);
 consumed=4;pgraph_vk_update_vertex_ram_buffer(&pg,8200,source+8200,16);assert(finishes==1&&settles==1&&!consumed&&r.vertex_ram_dirty.addr==8200&&r.vertex_ram_dirty.size==16);flush_memory_buffer(&pg,cmd);assert(calls[0]==4&&offsets[0]==8200&&sizes[0]==16);
 r.vertex_ram_coherent=true;pgraph_vk_update_vertex_ram_buffer(&pg,100,source+100,16);assert(!r.vertex_ram_dirty.size);flush_memory_buffer(&pg,cmd);assert(calls[0]==4&&!memcmp(mirror+100,source+100,16));r.vertex_ram_coherent=false;pgraph_vk_update_vertex_ram_buffer(&pg,0,source,0);flush_memory_buffer(&pg,cmd);assert(calls[0]==4);
 pgraph_vk_update_vertex_ram_buffer(&pg,0,source,sizeof(source));flush_memory_buffer(&pg,cmd);assert(calls[0]==5&&!offsets[0]&&sizes[0]==sizeof(source)&&!memcmp(mirror,source,sizeof(source))&&invalidations==9);
 puts("Vertex mirror: merged dirty envelope, clean/coherent skips, initial full upload, post-download/settle marking and payload bytes passed");return 0;}
'''
q=(r/'libretro/tests/test-spv-geom.c').read_text();qa=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');qb=q.index('int line_tests(',qa);prefix=prefix.replace('/* QUIET ERRORS */',q[qa:qb]).replace('/* RANGE TYPE */',range_type)
with tempfile.TemporaryDirectory(prefix='vk-arena-flush-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(prefix+s[a:b]+v[va:vb]+suffix);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)
