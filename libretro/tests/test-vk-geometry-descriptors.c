/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL GS transport line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define ROUND_UP(x,a) (((x)+(a)-1)/(a)*(a))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define NV2A_MAX_TEXTURES 4
#define PGRAPH_VK_SLOTS 2
#define VSH_UBO_BINDING 0
#define PSH_UBO_BINDING 1
#define PSH_TEX_BINDING 2
#define GSH_UBO_BINDING 14
#define VK_READY_GEOMETRY_BYTES 32u
#define BUFFER_UNIFORM 0
#define BUFFER_UNIFORM_STAGING 1
#define BUFFER_INDEX 2
#define BUFFER_VERTEX_INLINE 3
#define VK_FINISH_REASON_NEED_BUFFER_SPACE 1
static const struct{int staging,device;}pgraph_vk_staged_buffers[]={{BUFFER_UNIFORM_STAGING,BUFFER_UNIFORM}};
typedef struct{size_t total_size;void *allocation;}ShaderUniformLayout;
typedef struct ShaderModuleInfo{unsigned ready_vertex_index,ready_geometry_index;bool uniform_staged_valid;uint64_t uniform_staged_epoch;VkDeviceSize uniform_staged_offset;ShaderUniformLayout uniforms;}ShaderModuleInfo;
typedef struct{unsigned unused;}ShaderState;
typedef struct{ShaderState state;struct{ShaderModuleInfo *module_info;}vsh,geom,psh;}ShaderBinding;
typedef struct{VkPipelineLayout layout;}PipelineBinding;
typedef struct{VkBuffer buffer;VkDeviceSize buffer_offset,buffer_size;VkBufferUsageFlags usage;VkMemoryPropertyFlags properties;unsigned allocation;}StorageBuffer;
typedef struct{VkSampler sampler;VkImageView image_view;}TextureBinding;
typedef struct PGRAPHVkState{
 bool has_gs_ubo_binding;struct{bool enabled,draw;}shader_objects;
 struct{ShaderModuleInfo zero_uniform;}ready_geometry;
 unsigned device,allocator;VkDescriptorPool descriptor_pool;VkDescriptorSetLayout descriptor_set_layout;VkDescriptorSet descriptor_sets[8];int descriptor_set_index,descriptor_set_base;
 ShaderBinding*shader_binding;PipelineBinding*pipeline_binding;StorageBuffer storage_buffers[4];VkPhysicalDeviceProperties device_props;
 uint8_t gpu[4096];uint64_t uniform_upload_epoch;unsigned uniforms_changed;bool shader_bindings_changed,texture_bindings_changed;
 VkDeviceSize uniform_buffer_offsets[3],descriptor_uniform_sizes[3];VkBuffer descriptor_uniform_buffer;
 TextureBinding*texture_bindings[4];VkSampler texture_unit_sampler[4];VkCommandBuffer command_buffer;
 unsigned finishes,copies[3];
}PGRAPHVkState;
typedef struct{PGRAPHVkState*vk_renderer_state;}PGRAPHState;
static unsigned created_pool,created_layout,writes,binds,copy_calls,barrier_calls;
static unsigned expected_offsets,expected_writes,expected_pool;
static VkDeviceSize captured_ranges[3],captured_offsets[3];
static VkPipelineStageFlags destination_stages;
static VkAccessFlags destination_access;
static VkResult vkCreateDescriptorPool(unsigned d,const VkDescriptorPoolCreateInfo*i,void*a,VkDescriptorPool*p){(void)d;(void)a;assert(i->poolSizeCount==2&&i->pPoolSizes[0].descriptorCount==expected_pool&&i->maxSets==8);*p=(VkDescriptorPool)(uintptr_t)2;created_pool++;return VK_SUCCESS;}
static VkResult vkCreateDescriptorSetLayout(unsigned d,const VkDescriptorSetLayoutCreateInfo*i,void*a,VkDescriptorSetLayout*p){(void)d;(void)a;assert(i->bindingCount==expected_writes);for(unsigned j=0;j<i->bindingCount;j++){unsigned b=i->pBindings[j].binding;assert(b==(j<6?j:14));if(j==6)assert(i->pBindings[j].stageFlags==VK_SHADER_STAGE_GEOMETRY_BIT&&i->pBindings[j].descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);}*p=(VkDescriptorSetLayout)(uintptr_t)3;created_layout++;return VK_SUCCESS;}
static void vkUpdateDescriptorSets(unsigned d,unsigned count,const VkWriteDescriptorSet*w,unsigned c,void*p){(void)d;(void)p;assert(!c&&count==expected_writes);for(unsigned i=0;i<count;i++){assert(w[i].dstBinding==(i<6?i:14));if(i<2||i==6){unsigned index=i==6?2:i;captured_ranges[index]=w[i].pBufferInfo->range;if(index==2)assert(captured_ranges[index]==32);}}writes++;}
static VkPipelineLayout create_pipeline_layout(PGRAPHVkState*r,const ShaderState*s){(void)r;(void)s;return(VkPipelineLayout)(uintptr_t)4;}
static void vkCmdBindDescriptorSets(VkCommandBuffer c,VkPipelineBindPoint b,VkPipelineLayout l,unsigned first,unsigned count,const VkDescriptorSet*s,unsigned n,const uint32_t*o){(void)c;(void)b;(void)l;assert(!first&&count==1&&s&&n==expected_offsets);for(unsigned i=0;i<n;i++)captured_offsets[i]=o[i];binds++;}
static ShaderModuleInfo*vk_ready_geometry_uniform_owner(PGRAPHVkState*r,const ShaderBinding*b){return b->geom.module_info&&b->geom.module_info->ready_geometry_index?b->geom.module_info:&r->ready_geometry.zero_uniform;}
static VkDeviceSize pgraph_vk_ready_vertex_upload_extent(PGRAPHVkState*r,ShaderModuleInfo*m){(void)r;assert(!m->ready_vertex_index);return m->uniforms.total_size;}
static bool pgraph_vk_buffer_has_space_for(PGRAPHState*p,int i,VkDeviceSize n,size_t align){StorageBuffer*b=&p->vk_renderer_state->storage_buffers[i];return ROUND_UP(b->buffer_offset,align)+n<=b->buffer_size;}
static void pgraph_vk_finish(PGRAPHState*p,unsigned reason){PGRAPHVkState*r=p->vk_renderer_state;assert(reason==1);r->finishes++;r->storage_buffers[1].buffer_offset=0;r->descriptor_set_index=r->descriptor_set_base=0;memset(r->gpu,0xcd,sizeof(r->gpu));}
static VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState*p,int i,void**data,VkDeviceSize*sizes,unsigned n,size_t align){assert(n==1);PGRAPHVkState*r=p->vk_renderer_state;StorageBuffer*b=&r->storage_buffers[i];VkDeviceSize at=ROUND_UP(b->buffer_offset,align);assert(at+sizes[0]<=b->buffer_size);memcpy(r->gpu+at,*data,sizes[0]);b->buffer_offset=at+sizes[0];unsigned stage=*data==r->shader_binding->vsh.module_info->uniforms.allocation?0:*data==r->shader_binding->psh.module_info->uniforms.allocation?1:2;r->copies[stage]++;return at;}
static VkDeviceSize pgraph_vk_reserve_buffer(PGRAPHState*p,int i,VkDeviceSize n,size_t align,void**d){(void)p;(void)i;(void)n;(void)align;(void)d;assert(false);return 0;}
static void pgraph_vk_ready_vertex_upload(PGRAPHState*p,ShaderModuleInfo*m,void*d){(void)p;(void)m;(void)d;assert(false);}
static VkResult xemu_vk_flush_allocation(unsigned a,unsigned b,unsigned c,VkDeviceSize d){(void)a;(void)b;(void)c;(void)d;return VK_SUCCESS;}
static void vkCmdCopyBuffer(VkCommandBuffer c,VkBuffer a,VkBuffer b,unsigned n,const VkBufferCopy*r){(void)c;(void)a;(void)b;assert(n==1&&r->size);copy_calls++;}
static void vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags src,VkPipelineStageFlags dst,unsigned flags,unsigned mc,const void*m,unsigned bc,const VkBufferMemoryBarrier*b,unsigned ic,const void*i){(void)c;(void)flags;(void)m;(void)i;assert(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&!mc&&!ic&&bc==1);destination_stages=dst;destination_access=b[0].dstAccessMask;barrier_calls++;}
/* PRODUCTION */
static void upload(PGRAPHState*p,unsigned flags){PGRAPHVkState*r=p->vk_renderer_state;r->uniforms_changed=flags;pgraph_vk_update_descriptor_sets(p);bind_descriptor_sets(p);for(unsigned i=0;i<expected_offsets;i++)assert(captured_offsets[i]==r->uniform_buffer_offsets[i]);}
int main(void){unsigned cases=0;
 for(unsigned optional=0;optional<2;optional++){
  PGRAPHVkState r={0};PGRAPHState pg={&r};ShaderModuleInfo mods[4]={0};ShaderBinding binding={.vsh.module_info=&mods[0],.psh.module_info=&mods[1]};PipelineBinding pipeline={.layout=(VkPipelineLayout)(uintptr_t)4};uint32_t values[4][8]={{1},{2},{3},{4}},zero[8]={0};TextureBinding textures[4]={0};
  r.has_gs_ubo_binding=optional;r.shader_objects.enabled=r.shader_objects.draw=optional;expected_offsets=optional?3:2;expected_writes=optional?7:6;expected_pool=expected_offsets*8;
  r.shader_binding=&binding;r.pipeline_binding=&pipeline;r.device_props.limits.minUniformBufferOffsetAlignment=16;
  r.storage_buffers[1]=(StorageBuffer){.buffer=(VkBuffer)(uintptr_t)5,.buffer_size=4096,.properties=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};r.storage_buffers[0].buffer=(VkBuffer)(uintptr_t)6;
  r.ready_geometry.zero_uniform.uniforms=(ShaderUniformLayout){32,zero};
  for(unsigned i=0;i<4;i++){mods[i].uniforms=(ShaderUniformLayout){32,values[i]};if(i>=2)mods[i].ready_geometry_index=i-1;textures[i]=(TextureBinding){(VkSampler)(uintptr_t)(9+i),(VkImageView)(uintptr_t)(15+i)};r.texture_bindings[i]=&textures[i];}
  create_descriptor_pool(&pg);create_descriptor_set_layout(&pg);assert(created_pool==optional+1&&created_layout==optional+1);
  upload(&pg,optional?7:3);assert(r.descriptor_set_index==1&&r.copies[0]==1&&r.copies[1]==1&&r.copies[2]==optional);
  if(optional){
   assert(!memcmp(r.gpu+r.uniform_buffer_offsets[2],zero,32));binding.geom.module_info=&mods[2];upload(&pg,4);assert(!memcmp(r.gpu+r.uniform_buffer_offsets[2],values[2],32));size_t saved=r.uniform_buffer_offsets[2];
   binding.geom.module_info=&mods[3];upload(&pg,4);unsigned before=r.copies[2];binding.geom.module_info=&mods[2];upload(&pg,4);assert(r.copies[2]==before&&r.uniform_buffer_offsets[2]==saved);
   values[2][0]++;mods[2].uniform_staged_valid=false;upload(&pg,4);assert(r.uniform_buffer_offsets[2]!=saved&&r.gpu[saved]==3&&!memcmp(r.gpu+r.uniform_buffer_offsets[2],values[2],32));
   r.shader_objects.enabled=r.shader_objects.draw=false;binding.geom.module_info=NULL;upload(&pg,4);assert(!memcmp(r.gpu+r.uniform_buffer_offsets[2],zero,32)&&expected_offsets==3);
   binding.geom.module_info=&mods[2];r.storage_buffers[1].buffer_offset=0;upload(&pg,0);assert(!memcmp(r.gpu+r.uniform_buffer_offsets[2],values[2],32));
   unsigned finished=r.finishes;r.descriptor_set_index=4;r.texture_bindings_changed=true;upload(&pg,0);assert(r.finishes==finished+1&&!memcmp(r.gpu+r.uniform_buffer_offsets[2],values[2],32));r.texture_bindings_changed=false;
   r.uniform_upload_epoch++;memset(r.gpu,0xa7,sizeof(r.gpu));upload(&pg,4);assert(!memcmp(r.gpu+r.uniform_buffer_offsets[2],values[2],32));
   mods[2].uniform_staged_offset=UINT64_MAX;assert(!uniform_snapshot_valid(&r,&mods[2]));
  }
  for(unsigned declined=0;declined<2;declined++)for(unsigned vbo=0;vbo<2;vbo++){
   r.shader_objects.enabled=!declined;r.storage_buffers[1].buffer_offset=32;r.storage_buffers[0].usage=vbo?VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:0;sync_staging_buffers(&pg,VK_NULL_HANDLE);
   assert(!!(destination_stages&VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT)==optional);assert((destination_access&VK_ACCESS_UNIFORM_READ_BIT)&&!!(destination_stages&VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)==vbo);assert(!r.storage_buffers[1].buffer_offset);cases++;
  }
 }
 assert(copy_calls==8&&barrier_calls==8);printf("Vulkan GS descriptors: optional6/7bindings,2/3offsets,unchangedsetreuse,A/B/A,absentGS,declinedroute,rotation/epoch/overflow and %u immutableGS/vertex transfer-barrier cases PASS\n",cases);return 0;}
