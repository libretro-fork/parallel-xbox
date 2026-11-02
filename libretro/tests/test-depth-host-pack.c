/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ROUND_UP(a,b) (((a)+(b)-1)&~((b)-1))
typedef unsigned VkPipelineStageFlags;
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 1
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 2
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER 3
#define VK_ACCESS_TRANSFER_WRITE_BIT 1u
#define VK_ACCESS_TRANSFER_READ_BIT 2u
#define VK_ACCESS_SHADER_READ_BIT 4u
#define VK_ACCESS_SHADER_WRITE_BIT 8u
#define VK_ACCESS_HOST_READ_BIT 16u
#define VK_QUEUE_FAMILY_IGNORED 0xffffffffu
#define VK_PIPELINE_STAGE_TRANSFER_BIT 1u
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 2u
#define VK_PIPELINE_STAGE_HOST_BIT 4u
#define VK_PIPELINE_BIND_POINT_COMPUTE 1
#define VK_SHADER_STAGE_COMPUTE_BIT 1
#define VK_WHOLE_SIZE UINT64_MAX
#define RGBA_PINK 0
typedef unsigned VkBuffer,VkCommandBuffer,VkDescriptorSet,VkPipeline;
typedef uint64_t VkDeviceSize;
typedef struct {VkBuffer buffer;VkDeviceSize offset,range;} VkDescriptorBufferInfo;
typedef struct {int sType;VkDescriptorSet dstSet;unsigned dstBinding,descriptorType,descriptorCount;const VkDescriptorBufferInfo *pBufferInfo;} VkWriteDescriptorSet;
typedef struct {int sType;unsigned srcAccessMask,dstAccessMask,srcQueueFamilyIndex,dstQueueFamilyIndex;VkBuffer buffer;VkDeviceSize offset,size;} VkBufferMemoryBarrier;
typedef struct {VkDeviceSize srcOffset,dstOffset,size;} VkBufferCopy;
typedef struct {unsigned x,y;} ComputeGrid;
enum {BUFFER_COMPUTE_DST,BUFFER_COMPUTE_SRC,BUFFER_STAGING_DST};
typedef struct {unsigned device;struct {VkBuffer buffer;VkDeviceSize buffer_size;} storage_buffers[3];struct {struct {VkDeviceSize maxStorageBufferRange,minStorageBufferOffsetAlignment;} limits;} device_props;struct {VkDescriptorSet set_pack,set_unpack,set_pack_readback;unsigned pipeline_layout;} compute;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;unsigned scale;} PGRAPHState;
typedef struct {unsigned width,height;bool swizzle;uint64_t vram_addr;struct {unsigned bytes_per_pixel;} fmt;struct {int vk_format;} host_fmt;} SurfaceBinding;
static PGRAPHVkState state;
static VkDescriptorBufferInfo descriptor[4][3];
static uint32_t data[4][65536],ram[65536],push[4];
static unsigned bound,copies,stores,dispatches,writes,host_barriers,omitted_dependencies;
static unsigned latest[4];
static size_t expected_packed_size,expected_input_size;
static unsigned output_range_checks,input_range_checks,pre_batches;
static bool expected_host_pack;
static bool transfer_read[4],shader_read[4],host_visible[4];
static void vkUpdateDescriptorSets(unsigned d,unsigned n,const VkWriteDescriptorSet *w,unsigned count,void *p){assert(d==7&&n==9&&!count&&!p);for(unsigned i=0;i<n;i++){assert(w[i].dstSet>=1&&w[i].dstSet<=3&&w[i].dstBinding<3&&w[i].descriptorCount==1);descriptor[w[i].dstSet][w[i].dstBinding]=*w[i].pBufferInfo;}writes++;}
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *w,unsigned *h){*w*=pg->scale;*h*=pg->scale;}
static VkPipeline get_compute_pipeline(PGRAPHVkState *r,int format,bool pack){assert(r==&state&&format==1&&pack);return 40;}
static ComputeGrid get_dispatch(PGRAPHVkState *r,size_t n){assert(r==&state);return (ComputeGrid){(unsigned)((n+63)/64),1};}
static void pgraph_vk_begin_debug_marker(PGRAPHVkState *r,VkCommandBuffer cmd,int color,const char *s){assert(r==&state&&cmd==8&&color==0&&s);}
static void pgraph_vk_end_debug_marker(PGRAPHVkState *r,VkCommandBuffer cmd){assert(r==&state&&cmd==8);}
static void vkCmdBindPipeline(VkCommandBuffer cmd,int point,VkPipeline p){assert(cmd==8&&point==1&&p==40);}
static void vkCmdBindDescriptorSets(VkCommandBuffer cmd,int point,unsigned layout,unsigned first,unsigned count,const VkDescriptorSet *sets,unsigned offsets,void *p){assert(cmd==8&&point==1&&layout==10&&!first&&count==1&&!offsets&&!p);bound=*sets;}
static void vkCmdPushConstants(VkCommandBuffer cmd,unsigned layout,unsigned stage,unsigned offset,unsigned size,const void *p){assert(cmd==8&&layout==10&&stage==1&&!offset&&size==16);memcpy(push,p,16);}
static void vkCmdDispatch(VkCommandBuffer cmd,unsigned x,unsigned y,unsigned z){
 assert(cmd==8&&x&&y==1&&z==1&&(bound==1||bound==3));unsigned src=descriptor[bound][0].buffer,dst=descriptor[bound][2].buffer;
 assert(src==1&&shader_read[src]&&(dst==2||dst==3));unsigned inw=push[0],outw=push[1],base=push[2],n=push[3],scale=inw/outw;assert((uint64_t)n*4<=descriptor[bound][2].range);
 for(unsigned i=0;i<n;i++){unsigned si=((i/outw)*scale+scale/2)*inw+(i%outw)*scale+scale/2;uint8_t stencil=((uint8_t*)(data[src]+base))[si];data[dst][i]=((data[src][si]&0xffffffu)<<8)|stencil;}
 latest[dst]=VK_ACCESS_SHADER_WRITE_BIT;transfer_read[dst]=host_visible[dst]=false;dispatches++;
}
static void vkCmdPipelineBarrier(VkCommandBuffer cmd,unsigned src,unsigned dst,unsigned flags,unsigned mem,void *m,unsigned n,const VkBufferMemoryBarrier *barriers,unsigned images,void *im){
 assert(cmd==8&&!flags&&!mem&&!m&&(n==1||n==2)&&!images&&!im);
 if(dst==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT){
  assert(n==2&&src==(VK_PIPELINE_STAGE_TRANSFER_BIT|(expected_host_pack?VK_PIPELINE_STAGE_HOST_BIT:0)));
  assert(barriers[0].buffer==1&&barriers[1].buffer==(expected_host_pack?3u:2u));pre_batches++;
 }
 for(unsigned i=0;i<n;i++){
 const VkBufferMemoryBarrier *b=&barriers[i];assert(b->buffer>=1&&b->buffer<=3);
 unsigned id=b->buffer;
 if(id==1){assert(!b->offset&&b->size==expected_input_size);input_range_checks++;}
 if(id!=1&&(b->dstAccessMask==VK_ACCESS_SHADER_WRITE_BIT||
    (b->srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&b->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT))){
  assert(!b->offset&&b->size==expected_packed_size);output_range_checks++;
 }
 if(b->dstAccessMask==VK_ACCESS_SHADER_READ_BIT){assert((src&VK_PIPELINE_STAGE_TRANSFER_BIT)&&dst==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&(b->srcAccessMask&VK_ACCESS_TRANSFER_WRITE_BIT));shader_read[id]=true;}
 if(b->dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT){assert(src==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&b->srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT);transfer_read[id]=true;}
 if(b->dstAccessMask==VK_ACCESS_HOST_READ_BIT){assert(dst==VK_PIPELINE_STAGE_HOST_BIT&&b->srcAccessMask==latest[id]);assert(src==(latest[id]==VK_ACCESS_SHADER_WRITE_BIT?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT));host_visible[id]=true;host_barriers++;}
 if(b->dstAccessMask==VK_ACCESS_SHADER_WRITE_BIT&&id==3)assert((src&VK_PIPELINE_STAGE_HOST_BIT)&&(b->srcAccessMask&VK_ACCESS_HOST_READ_BIT));
 }
}

static void vkCmdCopyBuffer(VkCommandBuffer cmd,VkBuffer src,VkBuffer dst,unsigned count,const VkBufferCopy *r){assert(cmd==8&&src==2&&dst==3&&count==1&&transfer_read[src]&&!r->srcOffset&&!r->dstOffset);memcpy(data[dst],data[src],r->size);latest[dst]=VK_ACCESS_TRANSFER_WRITE_BIT;host_visible[dst]=false;copies++;}
static bool pgraph_vk_pack_depth_swizzled_possible(PGRAPHState *p,SurfaceBinding *s){(void)p;(void)s;return false;}
static void pgraph_vk_pack_depth_swizzled(PGRAPHState *p,SurfaceBinding *s,VkCommandBuffer c){(void)p;(void)s;(void)c;assert(false);}
static bool pgraph_vk_swizzle_packed_possible(PGRAPHState *pg,uint64_t offset,unsigned w,unsigned h,unsigned bpp){(void)offset;return (uint64_t)w*h*bpp<=pg->vk_renderer_state->device_props.limits.maxStorageBufferRange;}
static void store_packed_to_ram(PGRAPHState *pg,VkCommandBuffer cmd,SurfaceBinding *s,VkBuffer src,VkDeviceSize offset){assert(pg->vk_renderer_state==&state&&cmd==8&&src==2&&offset==16&&transfer_read[src]==!(s->swizzle&&pgraph_vk_swizzle_packed_possible(pg,s->vram_addr,s->width,s->height,4)));omitted_dependencies+=!transfer_read[src];memcpy(ram,data[src],(size_t)s->width*s->height*4);stores++;}
/* PRODUCTION */
int main(void){
 state.device=7;state.compute.set_pack=1;state.compute.set_unpack=2;state.compute.set_pack_readback=3;state.compute.pipeline_layout=10;state.device_props.limits.minStorageBufferOffsetAlignment=256;
 for(unsigned i=0;i<3;i++){state.storage_buffers[i].buffer=i+1;state.storage_buffers[i].buffer_size=sizeof(data[0]);}
 PGRAPHState pg={&state,1};unsigned host_cases=0,fallback=0,direct_cases=0;
 for(unsigned i=0;i<6000;i++){
  SurfaceBinding s={.width=1+i%47,.height=1+(i/47)%31,.swizzle=i%2,.fmt={4},.host_fmt={1}};pg.scale=1+i%3;
  unsigned iw=s.width*pg.scale,ih=s.height*pg.scale;size_t n=(size_t)s.width*s.height,bytes=n*4;
  bool direct=i%5==0;state.device_props.limits.maxStorageBufferRange=(i%7==0)?bytes-1:sizeof(data[0]);
  write_descriptor_sets(&pg);assert(descriptor[3][2].buffer==3&&descriptor[3][2].range==MIN(state.storage_buffers[2].buffer_size,state.device_props.limits.maxStorageBufferRange));
  size_t base=ROUND_UP((size_t)iw*ih*4,256)/4;assert(base*4+(size_t)iw*ih<=sizeof(data[0]));
  for(unsigned j=0;j<iw*ih;j++){data[1][j]=(j*97+i*31)&0xffffff;((uint8_t*)(data[1]+base))[j]=(uint8_t)(j+i);}
  expected_host_pack=!direct&&bytes<=state.device_props.limits.maxStorageBufferRange;
  expected_packed_size=bytes;expected_input_size=base*4+ROUND_UP((size_t)iw*ih,4);
  bool host=false;unsigned oldcopies=copies,oldstores=stores;VkBuffer out=pack_depth_readback(&pg,&s,8,1,bytes,direct,pg.scale!=1,16,&host);
  assert(host==(!direct&&bytes<=state.device_props.limits.maxStorageBufferRange));
  if(direct){assert(!host&&stores==oldstores+1&&copies==oldcopies);direct_cases++;}
  else{assert(out==3&&stores==oldstores);packed_readback_to_host_barrier(8,out,host);assert(host_visible[3]&&copies==oldcopies+!host);host?host_cases++:fallback++;}
  const uint32_t *actual=direct?ram:data[3];
  for(unsigned j=0;j<n;j++){unsigned si=((j/s.width)*pg.scale+pg.scale/2)*iw+(j%s.width)*pg.scale+pg.scale/2;uint32_t expected=(((si*97+i*31)&0xffffff)<<8)|(uint8_t)(si+i);assert(actual[j]==expected);}
 }
 assert(omitted_dependencies==514&&output_range_checks>6000&&input_range_checks==12000&&pre_batches==6000);
 assert(dispatches==6000&&host_cases&&fallback&&direct_cases&&host_barriers==host_cases+fallback);
 printf("Depth pre-pack dependencies: %u combined input/output batches passed\n",pre_batches);
 printf("Depth input plane ranges: %u scaled/aligned checks passed\n",input_range_checks);
 printf("Packed-depth output barrier ranges: %u exact-byte checks passed\n",output_range_checks);
 printf("Depth host pack: 6000 production descriptor/dispatch/visibility schedules; %u host packs omit GPU copy, %u oversized fallbacks and %u direct-RAM routes preserved; 514 stale transfer dependencies omitted; refreshed capped descriptors and exact output passed\n",host_cases,fallback,direct_cases);return 0;
}
