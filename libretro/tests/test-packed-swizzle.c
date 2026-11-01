/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define VK_NULL_HANDLE 0
#define VK_WHOLE_SIZE UINT64_MAX
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 1
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 2
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER 3
#define VK_ACCESS_TRANSFER_WRITE_BIT 1u
#define VK_ACCESS_TRANSFER_READ_BIT 2u
#define VK_ACCESS_SHADER_READ_BIT 4u
#define VK_ACCESS_SHADER_WRITE_BIT 8u
#define VK_PIPELINE_STAGE_TRANSFER_BIT 1u
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 2u
#define VK_QUEUE_FAMILY_IGNORED 0xffffffffu
#define VK_PIPELINE_BIND_POINT_COMPUTE 1
#define VK_SHADER_STAGE_COMPUTE_BIT 1
#define NV2A_PROF_SURF_SWIZZLE 1
#define UNSWIZZLE_WORKGROUP 256
typedef unsigned VkBuffer,VkDescriptorSet,VkCommandBuffer,VkPipelineStageFlags,VkAccessFlags;
typedef uint64_t VkDeviceSize,hwaddr;
typedef struct {VkBuffer buffer;VkDeviceSize offset,range;} VkDescriptorBufferInfo;
typedef struct {int sType;VkDescriptorSet dstSet;unsigned dstBinding,descriptorType,descriptorCount;const VkDescriptorBufferInfo *pBufferInfo;} VkWriteDescriptorSet;
typedef struct {int sType;unsigned srcAccessMask,dstAccessMask,srcQueueFamilyIndex,dstQueueFamilyIndex;VkBuffer buffer;VkDeviceSize offset,size;} VkBufferMemoryBarrier;
typedef struct {VkDeviceSize srcOffset,dstOffset,size;} VkBufferCopy;
enum {BUFFER_UNSWIZZLE_OUT,BUFFER_COMPUTE_SRC};
typedef struct {unsigned device;VkBuffer vram_buffer;struct {VkBuffer buffer;VkDeviceSize buffer_size;} storage_buffers[2];struct {struct {VkDeviceSize maxStorageBufferRange;unsigned maxComputeWorkGroupCount[3];} limits;} device_props;struct {bool ready;VkDescriptorSet set,packed_set;unsigned layout,swizzle_pipeline;} unswizzle;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;size_t *vram;} NV2AState;
#define memory_region_size(p) (*(p))
typedef struct {unsigned width,height,pitch;hwaddr vram_addr;bool swizzle;struct {unsigned bytes_per_pixel;} fmt;} SurfaceBinding;
/* PUSH */
static PGRAPHVkState state;
static uint8_t data[4][1<<20],expected[1<<20];
static VkDescriptorBufferInfo descriptors[2][2];
static struct UnswizzlePush push;
static unsigned bound,copies,dispatches,after,updates,producer_stage,producer_access;
static unsigned morton(unsigned x,unsigned y,unsigned w,unsigned h){unsigned out=0,bit=0;for(unsigned n=1;n<w||n<h;n<<=1){if(n<w){out|=!!(x&n)<<bit;bit++;}if(n<h){out|=!!(y&n)<<bit;bit++;}}return out;}
static void vkUpdateDescriptorSets(unsigned device,unsigned n,const VkWriteDescriptorSet *w,unsigned zero,void *p){assert(device==7&&n==2&&!zero&&!p);for(unsigned i=0;i<2;i++){assert(w[i].dstSet==11&&w[i].dstBinding==i&&w[i].descriptorCount==1);descriptors[1][i]=*w[i].pBufferInfo;}updates++;}
static void vkCmdPipelineBarrier(VkCommandBuffer cmd,unsigned src,unsigned dst,unsigned flags,unsigned mem,void *m,unsigned n,const VkBufferMemoryBarrier *b,unsigned images,void *im){assert(cmd==8&&!flags&&!mem&&!m&&n==1&&!images&&!im);if(b->dstAccessMask==VK_ACCESS_SHADER_READ_BIT){producer_stage=src;producer_access=b->srcAccessMask;assert(dst==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&b->buffer==(src==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT?3:2));}else{assert(src==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&b->buffer==3&&b->srcAccessMask==VK_ACCESS_SHADER_READ_BIT&&b->dstAccessMask==(VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT));after++;}}
static void vkCmdBindPipeline(VkCommandBuffer cmd,unsigned point,unsigned pipeline){assert(cmd==8&&point==1&&pipeline==40);}
static void vkCmdBindDescriptorSets(VkCommandBuffer cmd,unsigned point,unsigned layout,unsigned first,unsigned count,const VkDescriptorSet *set,unsigned offsets,void *p){assert(cmd==8&&point==1&&layout==41&&!first&&count==1&&!offsets&&!p);bound=*set;assert(bound==10||bound==11);}
static void vkCmdPushConstants(VkCommandBuffer cmd,unsigned layout,unsigned stage,unsigned offset,unsigned n,const void *p){assert(cmd==8&&layout==41&&stage==1&&!offset&&n==sizeof(push));memcpy(&push,p,n);}
static void vkCmdDispatch(VkCommandBuffer cmd,unsigned x,unsigned y,unsigned z){assert(cmd==8&&x==DIV_ROUND_UP(push.n_words,256)&&y==1&&z==1);unsigned set=bound-10,src=descriptors[set][1].buffer,dst=descriptors[set][0].buffer;assert(src==(set?3:2)&&dst==1&&push.depth==1&&push.n_words==push.width*push.height*push.bpp/4);assert(producer_stage==(set?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT)&&producer_access==(set?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_TRANSFER_WRITE_BIT));uint32_t mx=0,my=0;for(unsigned n=1,bit=0;n<push.width||n<push.height;n<<=1){if(n<push.width)mx|=1u<<bit++;if(n<push.height)my|=1u<<bit++;}assert(push.mask_x==mx&&push.mask_y==my&&!push.mask_z);for(unsigned yy=0;yy<push.height;yy++)for(unsigned xx=0;xx<push.width;xx++)memcpy(data[dst]+push.dst+morton(xx,yy,push.width,push.height)*push.bpp,data[src]+push.src+(yy*push.width+xx)*push.bpp,push.bpp);dispatches++;}
static void vkCmdCopyBuffer(VkCommandBuffer cmd,VkBuffer src,VkBuffer dst,unsigned n,const VkBufferCopy *regions){assert(cmd==8&&src==3&&(dst==1||dst==2));for(unsigned i=0;i<n;i++)memcpy(data[dst]+regions[i].dstOffset,data[src]+regions[i].srcOffset,regions[i].size);copies++;}
static void nv2a_profile_inc_counter(unsigned n){assert(n==1);}
/* PRODUCTION */
int main(void){size_t ram_size=sizeof(data[0]);state.device=7;state.vram_buffer=1;state.storage_buffers[0].buffer=2;state.storage_buffers[1].buffer=3;state.storage_buffers[0].buffer_size=state.storage_buffers[1].buffer_size=sizeof(data[0]);state.unswizzle.ready=true;state.unswizzle.set=10;state.unswizzle.packed_set=11;state.unswizzle.layout=41;state.unswizzle.swizzle_pipeline=40;state.device_props.limits.maxComputeWorkGroupCount[0]=65535;NV2AState d={.pgraph={&state},.vram=&ram_size};descriptors[0][0]=(VkDescriptorBufferInfo){1,0,VK_WHOLE_SIZE};descriptors[0][1]=(VkDescriptorBufferInfo){2,0,VK_WHOLE_SIZE};unsigned direct=0,fallback=0;
 for(unsigned i=0;i<6000;i++){SurfaceBinding s={.width=2u<<(i%6),.height=1u<<((i/6)%7),.vram_addr=32,.swizzle=true,.fmt={i%2?2:4}};s.pitch=s.width*s.fmt.bytes_per_pixel;size_t size=(size_t)s.width*s.height*s.fmt.bytes_per_pixel;state.device_props.limits.maxStorageBufferRange=i%5?sizeof(data[0]):size-1;pgraph_vk_unswizzle_packed_buffer_changed(&d.pgraph);assert(descriptors[1][0].buffer==1&&descriptors[1][1].buffer==3&&descriptors[1][1].range==MIN(state.storage_buffers[1].buffer_size,state.device_props.limits.maxStorageBufferRange));for(unsigned j=0;j<size;j++)data[3][j]=(uint8_t)(i+j*37);memset(data[1],0xCD,size+64);memset(expected,0xCD,size+64);for(unsigned yy=0;yy<s.height;yy++)for(unsigned xx=0;xx<s.width;xx++)memcpy(expected+s.vram_addr+morton(xx,yy,s.width,s.height)*s.fmt.bytes_per_pixel,data[3]+(yy*s.width+xx)*s.fmt.bytes_per_pixel,s.fmt.bytes_per_pixel);bool eligible=i%5!=0;unsigned oldcopies=copies,oldafter=after;assert(pgraph_vk_swizzle_packed_possible(&d.pgraph,s.vram_addr,s.width,s.height,s.fmt.bytes_per_pixel)==eligible);store_packed_to_ram(&d.pgraph,8,&s,3,64);assert(copies==oldcopies+!eligible&&after==oldafter+eligible);assert(!memcmp(data[1],expected,size+64));eligible?direct++:fallback++;}
 unsigned old=updates;state.unswizzle.packed_set=0;pgraph_vk_unswizzle_packed_buffer_changed(&d.pgraph);assert(updates==old&&!pgraph_vk_swizzle_packed_possible(&d.pgraph,32,8,8,4));state.unswizzle.packed_set=11;state.unswizzle.ready=false;assert(!pgraph_vk_swizzle_packed_possible(&d.pgraph,32,8,8,4));state.unswizzle.ready=true;assert(!pgraph_vk_swizzle_packed_possible(&d.pgraph,33,8,8,4));assert(!pgraph_vk_swizzle_packed_possible(&d.pgraph,ram_size-4,8,8,4));assert(dispatches==6000);printf("Packed swizzle: 6000 production routing/descriptor/barrier/Morton cases; %u GPU ring copies omitted, %u fallbacks preserved; bounds, capped descriptors and conversion-buffer reuse barrier passed\n",direct,fallback);return 0;}
