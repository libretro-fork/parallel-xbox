/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP(v,a) (((v)+(a)-1)&~((a)-1))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define NV2A_PROF_QUEUE_SUBMIT_AUX 0
/* CONSTANTS */
enum {BUFFER_STAGING_SRC,BUFFER_UNSWIZZLE_OUT};
typedef struct {size_t buffer_size;VkMemoryPropertyFlags properties;} StorageBuffer;
typedef struct {
 VkDevice device;
 /* AUX TYPE */
 int aux_slot,aux_oldest,aux_npending;
 bool in_aux_command_buffer;
 size_t ring_head[PGRAPH_VK_RINGS];
 StorageBuffer storage_buffers[2];
 struct {struct {VkDeviceSize nonCoherentAtomSize;} limits;} device_props;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static PGRAPHVkState state;
static PGRAPHState pg={&state};
#define CAPACITY 4096
static int owner[2][CAPACITY];
static uint32_t bytes[2][CAPACITY],expected[2][CAPACITY];
static bool signaled[PGRAPH_VK_AUX_SLOTS];
static unsigned serial[PGRAPH_VK_AUX_SLOTS],submit_serial;
static unsigned polls,waits,resets,growths,allocations;
static bool grow_noncoherent;
void pgraph_vk_wait_for_aux(PGRAPHState *pg);
static unsigned fence_slot(VkFence f){unsigned i=(unsigned)(uintptr_t)f-1;assert(i<PGRAPH_VK_AUX_SLOTS);return i;}
static void nv2a_profile_inc_counter(unsigned n){assert(n==NV2A_PROF_QUEUE_SUBMIT_AUX);}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device,VkFence fence){assert(device==state.device);unsigned s=fence_slot(fence);assert(state.aux[s].pending);polls++;return signaled[s]?VK_SUCCESS:VK_NOT_READY;}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device,uint32_t count,const VkFence *fences,VkBool32 all,uint64_t timeout){assert(device==state.device&&count==1&&all&&timeout==UINT64_MAX);unsigned s=fence_slot(fences[0]);assert(state.aux[s].pending);waits++;for(unsigned i=0;i<PGRAPH_VK_AUX_SLOTS;i++)if(state.aux[i].pending&&serial[i]<=serial[s])signaled[i]=true;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device,uint32_t count,const VkFence *fences){assert(device==state.device&&count==1);unsigned s=fence_slot(fences[0]);assert(state.aux[s].pending&&signaled[s]);for(unsigned r=0;r<2;r++)for(unsigned i=0;i<CAPACITY;i++)if(owner[r][i]==(int)s){assert(bytes[r][i]==expected[r][i]);owner[r][i]=-1;}signaled[s]=false;resets++;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer cmd,const VkCommandBufferBeginInfo *info){assert(state.in_aux_command_buffer&&cmd==state.aux[state.aux_slot].cmd&&!state.aux[state.aux_slot].pending&&info->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer cmd){assert(state.in_aux_command_buffer&&cmd==state.aux[state.aux_slot].cmd);return VK_SUCCESS;}
static void pgraph_vk_queue_submit(PGRAPHVkState *r,uint32_t count,const VkSubmitInfo *info,VkFence fence){assert(r==&state&&count==1&&info->commandBufferCount==1&&info->pCommandBuffers[0]==r->aux[r->aux_slot].cmd&&fence==r->aux[r->aux_slot].fence);unsigned s=fence_slot(fence);assert(!state.aux[s].pending&&!signaled[s]);serial[s]=++submit_serial;}
static void pgraph_vk_buffer_ensure_capacity(PGRAPHState *p,unsigned buffer,size_t size){assert(p==&pg&&buffer==BUFFER_STAGING_SRC&&size<=CAPACITY);pgraph_vk_wait_for_aux(p);state.storage_buffers[buffer].buffer_size=size;if(grow_noncoherent)state.storage_buffers[buffer].properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;growths++;}
/* PRODUCTION */
static void setup(size_t staging,size_t unswizzle){
 memset(&state,0,sizeof(state));memset(owner,0xff,sizeof(owner));memset(bytes,0,sizeof(bytes));memset(expected,0,sizeof(expected));memset(signaled,0,sizeof(signaled));memset(serial,0,sizeof(serial));submit_serial=polls=waits=resets=growths=allocations=0;state.device=(VkDevice)(uintptr_t)3;grow_noncoherent=false;state.device_props.limits.nonCoherentAtomSize=128;for(unsigned i=0;i<2;i++)state.storage_buffers[i].properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
 state.storage_buffers[0].buffer_size=staging;state.storage_buffers[1].buffer_size=unswizzle;
 for(unsigned i=0;i<PGRAPH_VK_AUX_SLOTS;i++){state.aux[i].cmd=(VkCommandBuffer)(uintptr_t)(0x100+i);state.aux[i].fence=(VkFence)(uintptr_t)(1+i);}
}
static VkDeviceSize allocate(unsigned ring,size_t size,size_t alignment){
 VkDeviceSize offset=UINT64_MAX;bool ok=pgraph_vk_ring_alloc(&pg,ring,size,alignment,&offset);if(!ok){assert(ring==PGRAPH_VK_RING_UNSWIZZLE&&offset==UINT64_MAX);return offset;}
 assert(offset%alignment==0&&offset+size<=state.storage_buffers[ring].buffer_size);unsigned s=state.aux_slot;uint32_t value=++allocations;
 size_t owned_end=offset+size;if(ring==PGRAPH_VK_RING_STAGING&&!(state.storage_buffers[ring].properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))owned_end=ROUND_UP(owned_end,state.device_props.limits.nonCoherentAtomSize);assert(owned_end<=CAPACITY);
 for(size_t i=offset;i<owned_end;i++){if(owner[ring][i]!=-1){fprintf(stderr,"Ring overlap ring=%u offset=%llu size=%zu byte=%zu owner=%d active=%u head=%zu pending=%d oldest=%d\n",ring,(unsigned long long)offset,size,i,owner[ring][i],s,state.ring_head[ring],state.aux_npending,state.aux_oldest);for(unsigned k=0;k<PGRAPH_VK_AUX_SLOTS;k++)fprintf(stderr,"slot %u pending=%u ready=%u start=%zu used=%u\n",k,state.aux[k].pending,signaled[k],state.aux[k].ring_start[ring],state.aux[k].ring_used[ring]);abort();}owner[ring][i]=s;bytes[ring][i]=expected[ring][i]=value;}
 return offset;
}
static void submit(VkCommandBuffer cmd){pgraph_vk_end_single_time_commands_async(&pg,cmd);assert(state.aux_npending<=PGRAPH_VK_AUX_SLOTS);}
static void finish(void){pgraph_vk_wait_for_aux(&pg);assert(!state.aux_npending);for(unsigned r=0;r<2;r++)for(unsigned i=0;i<CAPACITY;i++)assert(owner[r][i]==-1);}
static uint32_t rng=0x842a7913;
static unsigned random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 quiet_test_errors();
 /* A long recording fits alongside an unfinished submission. The old eager
  * loop queried its fence before each allocation; fitting must not need it. */
 for(unsigned eager=0;eager<2;eager++){
  setup(CAPACITY,CAPACITY);VkCommandBuffer cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(0,16,1)==0);submit(cmd);cmd=pgraph_vk_begin_single_time_commands(&pg);
  for(unsigned i=0;i<1000;i++){if(eager)while(state.aux_npending&&aux_retire_oldest(&state,true)){}assert(allocate(0,1,1)==16+i);}
  assert(polls==(eager?1000u:0u)&&!waits&&state.aux[0].pending);submit(cmd);finish();
 }
 puts("Ring allocation: 1000 eager fence-status calls -> zero when space fits");
 for(unsigned complete=0;complete<2;complete++){
  setup(64,64);VkCommandBuffer cmd=pgraph_vk_begin_single_time_commands(&pg);allocate(0,32,1);submit(cmd);signaled[0]=complete;cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(0,16,1)==32&&!polls&&!waits);assert(allocate(0,24,1)==0&&polls==1&&waits==!complete);submit(cmd);finish();
 }
 /* One retired slot opens enough room: do not poll the next unfinished one. */
 setup(64,64);VkCommandBuffer cmd=pgraph_vk_begin_single_time_commands(&pg);allocate(0,16,1);submit(cmd);cmd=pgraph_vk_begin_single_time_commands(&pg);allocate(0,16,1);submit(cmd);signaled[0]=true;cmd=pgraph_vk_begin_single_time_commands(&pg);allocate(0,32,1);assert(allocate(0,8,1)==0&&polls==1&&!waits&&state.aux[1].pending);submit(cmd);finish();
 /* Grow only an unused staging recording after pending readers retire. */
 setup(32,64);cmd=pgraph_vk_begin_single_time_commands(&pg);allocate(0,8,1);submit(cmd);cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(0,64,16)==0&&growths==1&&waits==1&&!polls&&state.storage_buffers[0].buffer_size==128);submit(cmd);finish();
 setup(64,64);cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(1,64,16)==UINT64_MAX&&!polls&&!waits&&!growths);allocate(1,48,16);assert(allocate(1,32,16)==UINT64_MAX&&!polls&&!waits);submit(cmd);finish();
 /* Regression: a full wrapped recording has equal start/end positions.
  * It must remain owned both before and after submitting the recording. */
 setup(64,64);state.ring_head[1]=16;cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(1,32,1)==16&&allocate(1,16,1)==48&&allocate(1,16,1)==0);assert(state.aux[0].ring_start[1]==state.ring_head[1]&&state.aux[0].ring_used[1]);assert(allocate(1,1,1)==UINT64_MAX&&!polls&&!waits);submit(cmd);cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(1,1,1)==16&&polls==1&&waits==1);submit(cmd);finish();
 puts("Ring regression: exact-full wrapped recording and submitted ranges retain ownership");
 /* Non-coherent staging allocations reserve distinct flush atoms, including
  * after a growth that changes the allocation's memory type. */
 setup(512,64);state.storage_buffers[0].properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
 for(unsigned i=0;i<4;i++){cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(0,1,16)==i*128);submit(cmd);}assert(!polls&&!waits);cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(0,1,16)==0&&polls==1&&waits==1);submit(cmd);finish();
 setup(32,64);grow_noncoherent=true;cmd=pgraph_vk_begin_single_time_commands(&pg);assert(allocate(0,64,16)==0&&growths==1&&!(state.storage_buffers[0].properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));submit(cmd);finish();
 puts("Staging ring: distinct non-coherent flush atoms and post-growth memory-type alignment passed");
 unsigned total_allocations=0,total_polls=0,total_waits=0;
 for(unsigned run=0;run<100;run++){
  setup(run&1?4096:256,128);
  if(run&1){state.storage_buffers[0].properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;state.device_props.limits.nonCoherentAtomSize=64u<<(run%4);}
  for(unsigned frame=0;frame<200;frame++){
   for(unsigned i=0;i<PGRAPH_VK_AUX_SLOTS;i++)if(state.aux[i].pending&&random_word()%4==0)signaled[i]=true;
   cmd=pgraph_vk_begin_single_time_commands(&pg);
   for(unsigned i=0;i<2;i++)allocate(0,1+random_word()%16,1u<<(random_word()%4));
   for(unsigned i=0,n=1+random_word()%6;i<n;i++)allocate(1,1+random_word()%32,1u<<(random_word()%5));
   submit(cmd);
   if(random_word()%31==0)pgraph_vk_wait_for_aux(&pg);
  }
  finish();total_allocations+=allocations;total_polls+=polls;total_waits+=waits;
 }
 printf("Ring ownership: %u allocations over 20000 delayed submissions passed, two rings, wrap/alignment, pressure-only polling, oldest-first waits, growth, exhaustion and slot reuse (polls=%u waits=%u)\n",total_allocations,total_polls,total_waits);
 return 0;
}
