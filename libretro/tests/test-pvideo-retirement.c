/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define PGRAPH_VK_AUX_SLOTS 8
#define PGRAPH_VK_RINGS 2
#define VK_SUCCESS 0
#define VK_TRUE 1
#define VK_NULL_HANDLE 0
#define VK_WHOLE_SIZE UINT64_MAX
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 1
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 2
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 3
#define VK_DESCRIPTOR_TYPE_STORAGE_IMAGE 4
#define VK_IMAGE_LAYOUT_GENERAL 5
#define BUFFER_STAGING_SRC 0
#define NV2A_PROF_QUEUE_SUBMIT_AUX 0
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define g_renew(type,p,n) ((type*)grow(p,(n)*sizeof(type)))
typedef unsigned VkFence,VkCommandBuffer,VkPipeline,VkPipelineLayout,VkSampler,VkFramebuffer,VkImageView,VkImage,VmaAllocation,VkBuffer,VkImageCreateInfo;
typedef struct {int sType;unsigned commandBufferCount;const VkCommandBuffer *pCommandBuffers;} VkSubmitInfo;
typedef struct {VkBuffer buffer;uint64_t range;} VkDescriptorBufferInfo;
typedef struct {VkImageView imageView;int imageLayout;} VkDescriptorImageInfo;
typedef struct {int sType;unsigned dstSet,dstBinding,descriptorCount;int descriptorType;const VkDescriptorBufferInfo *pBufferInfo;const VkDescriptorImageInfo *pImageInfo;} VkWriteDescriptorSet;
/* TRASH TYPE */
typedef struct {unsigned sampler,image_view,image,allocation;VkImageCreateInfo image_ci;struct {unsigned sets[8],source[8],output[8];} convert;} Video;
typedef struct {Video pvideo;uint64_t submitted,settled;} PGRAPHVkDisplayState;
typedef struct {
 unsigned device,allocator;
 struct PGRAPHVkAuxSlot {VkCommandBuffer cmd;VkFence fence;uint64_t serial;bool pending;bool ring_used[2];size_t ring_start[2];} aux[8];
 int aux_slot,aux_oldest,aux_npending;bool in_aux_command_buffer,in_command_buffer;
 uint64_t aux_submitted,aux_settled;
 uint32_t submit_count,settled_submit;
 PGRAPHVkDisplayState display;
 struct XemuVkTrashEntry *trash;int trash_n,trash_cap;
 struct {VkBuffer buffer;} storage_buffers[1];
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static PGRAPHVkState state;
static uint64_t gpu_aux,gpu_display;
static unsigned descriptors[8],sources[8],bound[8],destroyed[100000],next_image,waits,polls,updates,growths;
static uint64_t last_aux[100000],last_display[100000];
static void *grow(void *p,size_t n){growths++;p=realloc(p,n);assert(p);return p;}
static void nv2a_profile_inc_counter(int n){assert(n==0);}
static int vkGetFenceStatus(unsigned device,VkFence fence){assert(device==7&&fence>=1&&fence<=8);polls++;return state.aux[fence-1].serial<=gpu_aux?0:1;}
static int vkWaitForFences(unsigned device,unsigned count,const VkFence *f,unsigned all,uint64_t timeout){assert(device==7&&count==1&&all&&timeout==UINT64_MAX);gpu_aux=state.aux[*f-1].serial;waits++;return 0;}
static int vkResetFences(unsigned device,unsigned count,const VkFence *f){assert(device==7&&count==1&&state.aux[*f-1].serial<=gpu_aux);return 0;}
static int vkEndCommandBuffer(VkCommandBuffer cmd){assert(cmd==state.aux[state.aux_slot].cmd);return 0;}
static void pgraph_vk_queue_submit(PGRAPHVkState *r,unsigned count,const VkSubmitInfo *info,VkFence fence){
 unsigned slot=r->aux_slot;assert(r==&state&&count==1&&info->commandBufferCount==1&&*info->pCommandBuffers==slot+1&&fence==slot+1);
 assert(bound[slot]&&!destroyed[bound[slot]]);last_aux[bound[slot]]=r->aux_submitted+1;
}
static void vkUpdateDescriptorSets(unsigned device,unsigned count,const VkWriteDescriptorSet *w,unsigned copies,void *unused){
 assert(device==7&&count==2&&!copies&&!unused);unsigned slot=w[0].dstSet-1;
 assert(slot<8&&w[1].dstSet==w[0].dstSet&&!state.aux[slot].pending);
 assert(w[0].dstBinding==0&&w[1].dstBinding==1&&w[0].descriptorCount==1&&w[1].descriptorCount==1);
 sources[slot]=w[0].pBufferInfo->buffer;descriptors[slot]=w[1].pImageInfo->imageView;updates++;
}
static void pgraph_vk_poll_display_retired(PGRAPHVkState *r){r->display.settled=gpu_display;}
static bool pgraph_vk_image_pool_put(PGRAPHVkState *r,VkImage im,VmaAllocation a,const VkImageCreateInfo *ci){(void)r;assert(im==a&&ci);assert(last_aux[im]<=gpu_aux&&last_display[im]<=gpu_display);return false;}
static void check_destroy(unsigned h){assert(h&&!destroyed[h]&&last_aux[h]<=gpu_aux&&last_display[h]<=gpu_display);}
static void vkDestroySampler(unsigned d,unsigned h,void *p){assert(d==7&&!p);check_destroy(h);}
static void vkDestroyImageView(unsigned d,unsigned h,void *p){assert(d==7&&!p);check_destroy(h);}
static void vmaDestroyImage(unsigned a,VkImage im,VmaAllocation alloc){assert(a==9&&im==alloc);check_destroy(im);destroyed[im]++;}
static void vkDestroyPipeline(unsigned d,unsigned h,void *p){(void)d;(void)h;(void)p;assert(false);}
static void vkDestroyPipelineLayout(unsigned d,unsigned h,void *p){(void)d;(void)h;(void)p;assert(false);}
static void vkDestroyFramebuffer(unsigned d,unsigned h,void *p){(void)d;(void)h;(void)p;assert(false);}
/* PRODUCTION */
static void replace(void){
 if(state.display.pvideo.image)retire_pvideo_image(&state);
 for(unsigned i=0;i<8;i++)assert(!state.display.pvideo.convert.output[i]);
 unsigned im=++next_image;assert(im<100000);state.display.pvideo.image=im;state.display.pvideo.image_view=im;state.display.pvideo.sampler=im;state.display.pvideo.allocation=im;
}
static void submit(void){
 PGRAPHState pg={&state};unsigned slot=state.aux_slot;
 while(state.aux[slot].pending)assert(aux_retire_oldest(&state,false));
 state.in_aux_command_buffer=true;
 unsigned prior=updates;pvideo_convert_bind_source(&state);
 assert(updates<=prior+1&&descriptors[slot]==state.display.pvideo.image_view&&sources[slot]==state.storage_buffers[0].buffer);
 pvideo_convert_bind_source(&state);assert(updates<=prior+1); /* identical reuse */
 bound[slot]=descriptors[slot];
 pgraph_vk_end_single_time_commands_async(&pg,slot+1);
}
static void display(void){unsigned im=state.display.pvideo.image;last_display[im]=++state.display.submitted;}
int main(void){
 state.device=7;state.allocator=9;state.trash_cap=512;state.trash=calloc(512,sizeof(*state.trash));assert(state.trash);state.storage_buffers[0].buffer=123;
 for(unsigned i=0;i<8;i++){state.aux[i].cmd=i+1;state.aux[i].fence=i+1;state.display.pvideo.convert.sets[i]=i+1;}
 replace();submit();display();replace();
 /* Display done while the last compute upload is still outstanding. */
 gpu_display=state.display.submitted;trash_drain_retired(&state);assert(!destroyed[1]&&state.trash_n==1&&!waits);
 gpu_aux=state.aux_submitted;trash_drain_retired(&state);assert(destroyed[1]&&state.trash_n==0);
 submit();display();replace();gpu_aux=state.aux_submitted;trash_drain_retired(&state);assert(!destroyed[2]);
 gpu_display=state.display.submitted;trash_drain_retired(&state);assert(destroyed[2]);
 /* An open main recording remains protected by the existing trash key. */
 state.in_command_buffer=true;replace();state.in_command_buffer=false;
 gpu_aux=state.aux_submitted;gpu_display=state.display.submitted;
 trash_drain_retired(&state);assert(state.trash_n==1);
 state.submit_count++;state.settled_submit=state.submit_count;
 trash_drain_retired(&state);assert(!state.trash_n);
 /* No display ever consumed this upload: its aux fence still protects it. */
 submit();replace();trash_drain_retired(&state);assert(!destroyed[4]);gpu_aux=state.aux_submitted;trash_drain_retired(&state);assert(destroyed[4]);
 for(unsigned i=0;i<20000;i++){
  if(i%3==0)replace();
  submit();if(i%5)display();
  if(i%7==0)gpu_aux=state.aux_submitted;
  if(i%11==0)gpu_display=state.display.submitted;
  state.settled_submit=state.submit_count;unsigned old=waits;trash_drain_retired(&state);assert(waits==old);
 }
 retire_pvideo_image(&state);gpu_aux=state.aux_submitted;gpu_display=state.display.submitted;trash_drain_retired(&state);
 assert(!state.trash_n&&!growths);for(unsigned i=1;i<=next_image;i++)assert(destroyed[i]==1);
 printf("PVIDEO retirement: 20000 resize/upload/display schedules; independent aux/display completion, unconsumed uploads and safe descriptor-slot reuse; zero waits during retirement, zero trash growth; %u images destroyed exactly once\n",next_image);
 free(state.trash);return 0;
}
