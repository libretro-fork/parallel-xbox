# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check production vertex-arena flush ranges and unchanged GPU visibility barriers."""
from pathlib import Path
import ctypes,os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();a=s.index('static void flush_memory_buffer(');b=s.index('{',a)+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define BUFFER_VERTEX_RAM 0
typedef struct {VkBuffer buffer;uintptr_t allocation;VkDeviceSize buffer_offset;} StorageBuffer;
typedef struct {void *allocator;StorageBuffer storage_buffers[1],vertex_arena[2];bool vertex_arena_coherent[2];VkDeviceSize vertex_arena_dirty_start[2];} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static unsigned calls[3],barriers;static VkDeviceSize offsets[3],sizes[3];
static VkResult vmaFlushAllocation(void *allocator,uintptr_t allocation,VkDeviceSize offset,VkDeviceSize size){assert(allocator==(void*)(uintptr_t)7&&allocation>=1&&allocation<=3);unsigned i=allocation-1;calls[i]++;offsets[i]=offset;sizes[i]=size;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier *memory,uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images){assert(cmd==(VkCommandBuffer)(uintptr_t)4&&src==VK_PIPELINE_STAGE_HOST_BIT&&dst==VK_PIPELINE_STAGE_VERTEX_INPUT_BIT&&!flags&&!memory_count&&!memory&&buffer_count==3&&!image_count&&!images);for(unsigned i=0;i<3;i++){assert(buffers[i].sType==VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER&&buffers[i].srcAccessMask==VK_ACCESS_HOST_WRITE_BIT&&buffers[i].dstAccessMask==VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT&&buffers[i].buffer==(VkBuffer)(uintptr_t)(10+i)&&buffers[i].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&buffers[i].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&!buffers[i].offset&&buffers[i].size==VK_WHOLE_SIZE);}barriers++;}
'''
suffix=r'''
int main(void){quiet_test_errors();PGRAPHVkState r={.allocator=(void*)(uintptr_t)7,.storage_buffers={{(VkBuffer)(uintptr_t)10,1,0}},.vertex_arena={{(VkBuffer)(uintptr_t)11,2,4096},{(VkBuffer)(uintptr_t)12,3,6000}},.vertex_arena_coherent={false,true},.vertex_arena_dirty_start={UINT64_MAX,UINT64_MAX}};PGRAPHState pg={&r};VkCommandBuffer cmd=(VkCommandBuffer)(uintptr_t)4;
 flush_memory_buffer(&pg,cmd);assert(calls[0]==1&&!calls[1]&&!calls[2]&&barriers==1&&sizes[0]==VK_WHOLE_SIZE);
 r.vertex_arena_dirty_start[0]=1024;r.vertex_arena_dirty_start[1]=0;flush_memory_buffer(&pg,cmd);assert(calls[1]==1&&offsets[1]==1024&&sizes[1]==3072&&!calls[2]&&r.vertex_arena_dirty_start[0]==UINT64_MAX&&r.vertex_arena_dirty_start[1]==UINT64_MAX);
 flush_memory_buffer(&pg,cmd);assert(calls[1]==1&&!calls[2]);
 r.vertex_arena[0].buffer_offset=5000;r.vertex_arena_dirty_start[0]=4096;flush_memory_buffer(&pg,cmd);assert(calls[1]==2&&offsets[1]==4096&&sizes[1]==904);
 r.vertex_arena[1].buffer_offset=0;r.vertex_arena_dirty_start[1]=UINT64_MAX;r.vertex_arena_coherent[1]=false;flush_memory_buffer(&pg,cmd);assert(!calls[2]);
 r.vertex_arena[1].buffer_offset=100;r.vertex_arena_dirty_start[1]=0;flush_memory_buffer(&pg,cmd);assert(calls[2]==1&&offsets[2]==0&&sizes[2]==100);
 assert(calls[0]==6&&barriers==6);puts("Vertex flush: clean/coherent arenas skip allocator calls; non-coherent dirty tails and recycled arenas use exact byte ranges; all mirror/arena visibility barriers preserved");return 0;}
'''
q=(r/'libretro/tests/test-spv-geom.c').read_text();qa=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');qb=q.index('int line_tests(',qa);prefix=prefix.replace('/* QUIET ERRORS */',q[qa:qb])
with tempfile.TemporaryDirectory(prefix='vk-arena-flush-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(prefix+s[a:b]+suffix);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)
