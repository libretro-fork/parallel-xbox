/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/depth.h"
#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ROUND_UP(a,b) (((a)+(b)-1)&~((b)-1))
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define H(type,n) ((type)(uintptr_t)(n))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define memory_region_size(p) (*(p))
#define UNSWIZZLE_WORKGROUP 256
#define NV2A_PROF_SURF_SWIZZLE 1
typedef uint64_t hwaddr;
enum {BUFFER_COMPUTE_DST,BUFFER_COMPUTE_SRC,BUFFER_UNSWIZZLE_OUT};
typedef struct {
    VkDevice device; VkBuffer vram_buffer; VkPipelineCache vk_pipeline_cache;
    VkPhysicalDeviceProperties device_props;
    struct {VkBuffer buffer; VkDeviceSize buffer_size;} storage_buffers[3];
    struct {unsigned workgroup_size;} compute;
    struct {
        VkDescriptorPool pool; VkDescriptorSetLayout set_layout,depth_set_layout;
        VkDescriptorSet set,packed_set,depth_set;
        VkPipelineLayout layout,depth_layout;
        VkPipeline pipeline,swizzle_pipeline,depth_pipelines[2];bool ready;
    } unswizzle;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;unsigned scale;} PGRAPHState;
typedef struct {PGRAPHState pgraph;size_t *vram;} NV2AState;
typedef struct {unsigned width,height;hwaddr vram_addr;bool swizzle;struct {unsigned bytes_per_pixel;} fmt;struct {VkFormat vk_format;} host_fmt;} SurfaceBinding;
static PGRAPHVkState state;
static unsigned sets,layouts,modules,pipelines,updates,dispatches,dropped_pipelines,dropped_layouts,dropped_sets,dropped_pools;
static uint32_t last_push[5];
static VkPipeline bound_pipeline;
static void pgraph_apply_scaling_factor(PGRAPHState *p,unsigned *w,unsigned *h){*w*=p->scale;*h*=p->scale;}
static void nv2a_profile_inc_counter(unsigned n){assert(n==1);}
static VkShaderModule pgraph_vk_create_shader_module_from_words(PGRAPHVkState *r,const uint32_t *w,size_t n){assert(r==&state&&modules<2);SpvStaticModule s=pgraph_spv_depth_swizzled(modules,state.compute.workgroup_size);assert(s.words==w&&s.num_words==n);return H(VkShaderModule,++modules);}
static VkResult spy_set_layout(VkDevice d,const VkDescriptorSetLayoutCreateInfo *i,const VkAllocationCallbacks *a,VkDescriptorSetLayout *out){(void)d;assert(!a&&i->bindingCount==3);for(unsigned n=0;n<3;n++)assert(i->pBindings[n].binding==n&&i->pBindings[n].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&i->pBindings[n].descriptorCount==1);*out=H(VkDescriptorSetLayout,15);layouts++;return VK_SUCCESS;}
static VkResult spy_sets(VkDevice d,const VkDescriptorSetAllocateInfo *i,VkDescriptorSet *out){(void)d;assert(i->descriptorPool==H(VkDescriptorPool,7)&&i->descriptorSetCount==1&&*i->pSetLayouts==H(VkDescriptorSetLayout,15));*out=H(VkDescriptorSet,12);sets++;return VK_SUCCESS;}
static void spy_update(VkDevice d,unsigned count,const VkWriteDescriptorSet *w,unsigned copies,const VkCopyDescriptorSet *copy){(void)d;assert(!copies&&!copy&&(count==2||count==3));for(unsigned n=0;n<count;n++){assert(w[n].descriptorCount==1&&w[n].dstBinding==n);VkDescriptorBufferInfo b=*w[n].pBufferInfo;if(count==3){assert(w[n].dstSet==H(VkDescriptorSet,12));assert(b.buffer==(n==2?state.vram_buffer:state.storage_buffers[0].buffer));assert(b.range==(n==2?VK_WHOLE_SIZE:MIN(state.storage_buffers[0].buffer_size,state.device_props.limits.maxStorageBufferRange)));}else{assert(w[n].dstSet==H(VkDescriptorSet,11));assert(b.buffer==(n?state.storage_buffers[1].buffer:state.vram_buffer));}}updates++;}
static VkResult spy_layout(VkDevice d,const VkPipelineLayoutCreateInfo *i,const VkAllocationCallbacks *a,VkPipelineLayout *out){(void)d;assert(!a&&i->setLayoutCount==1&&*i->pSetLayouts==H(VkDescriptorSetLayout,15)&&i->pushConstantRangeCount==1&&i->pPushConstantRanges->size==20);*out=H(VkPipelineLayout,16);layouts++;return VK_SUCCESS;}
static VkResult spy_pipelines(VkDevice d,VkPipelineCache c,unsigned count,const VkComputePipelineCreateInfo *i,const VkAllocationCallbacks *a,VkPipeline *out){(void)d;(void)c;assert(!a&&count==1&&pipelines<2&&i->layout==H(VkPipelineLayout,16)&&i->stage.module==H(VkShaderModule,pipelines+1));*out=H(VkPipeline,100+pipelines++);return VK_SUCCESS;}
static void spy_drop_module(VkDevice d,VkShaderModule m,const VkAllocationCallbacks *a){(void)d;assert(!a&&m);}
static void spy_bind(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline pipeline){assert(c==H(VkCommandBuffer,8)&&p==VK_PIPELINE_BIND_POINT_COMPUTE);bound_pipeline=pipeline;}
static void spy_bind_set(VkCommandBuffer c,VkPipelineBindPoint p,VkPipelineLayout l,unsigned first,unsigned count,const VkDescriptorSet *s,unsigned offsets,const uint32_t *o){assert(c==H(VkCommandBuffer,8)&&p==VK_PIPELINE_BIND_POINT_COMPUTE&&l==H(VkPipelineLayout,16)&&!first&&count==1&&*s==H(VkDescriptorSet,12)&&!offsets&&!o);}
static void spy_push(VkCommandBuffer c,VkPipelineLayout l,VkShaderStageFlags s,unsigned offset,unsigned size,const void *p){assert(c==H(VkCommandBuffer,8)&&l==H(VkPipelineLayout,16)&&s==VK_SHADER_STAGE_COMPUTE_BIT&&!offset&&size==20);memcpy(last_push,p,20);}
static bool ram_claimed;
static void lr_gpu_ram_claim(uintptr_t cmd,uint64_t addr,uint64_t len){assert(cmd==8 && !(addr&3) && len);ram_claimed=true;}
static void spy_dispatch(VkCommandBuffer c,unsigned x,unsigned y,unsigned z){assert(ram_claimed);ram_claimed=false;assert(c==H(VkCommandBuffer,8)&&z==1&&x&&y&&(uint64_t)x*y*state.compute.workgroup_size>=last_push[3]);dispatches++;}
static void spy_drop_pipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks *a){(void)d;assert(!a&&p==H(VkPipeline,100+dropped_pipelines));dropped_pipelines++;}
static void spy_drop_layout(VkDevice d,VkPipelineLayout l,const VkAllocationCallbacks *a){(void)d;assert(!a&&l==H(VkPipelineLayout,16));dropped_layouts++;}
static void spy_drop_set(VkDevice d,VkDescriptorSetLayout l,const VkAllocationCallbacks *a){(void)d;assert(!a&&l==H(VkDescriptorSetLayout,15));dropped_sets++;}
static void spy_drop_pool(VkDevice d,VkDescriptorPool p,const VkAllocationCallbacks *a){(void)d;assert(!a&&p==H(VkDescriptorPool,7));dropped_pools++;}
#define vkCreateDescriptorSetLayout spy_set_layout
#define vkAllocateDescriptorSets spy_sets
#define vkUpdateDescriptorSets spy_update
#define vkCreatePipelineLayout spy_layout
#define vkCreateComputePipelines spy_pipelines
#define vkDestroyShaderModule spy_drop_module
#define vkCmdBindPipeline spy_bind
#define vkCmdBindDescriptorSets spy_bind_set
#define vkCmdPushConstants spy_push
#define vkCmdDispatch spy_dispatch
#define vkDestroyPipeline spy_drop_pipeline
#define vkDestroyPipelineLayout spy_drop_layout
#define vkDestroyDescriptorSetLayout spy_drop_set
#define vkDestroyDescriptorPool spy_drop_pool
/* PRODUCTION */
int main(void){
    size_t ram=64<<20;NV2AState d={.pgraph={&state,1},.vram=&ram};
    state.vram_buffer=H(VkBuffer,10);state.unswizzle.pool=H(VkDescriptorPool,7);state.unswizzle.packed_set=H(VkDescriptorSet,11);state.unswizzle.ready=true;
    state.device_props.limits.maxStorageBufferRange=128<<20;state.device_props.limits.minStorageBufferOffsetAlignment=256;state.device_props.limits.maxComputeWorkGroupCount[0]=65535;state.device_props.limits.maxComputeWorkGroupCount[1]=65535;
    state.compute.workgroup_size=128;
    for(unsigned n=0;n<3;n++){state.storage_buffers[n].buffer=H(VkBuffer,n+1);state.storage_buffers[n].buffer_size=16<<20;}
    init_depth_swizzle(&d.pgraph);assert(modules==2&&pipelines==2&&sets==1&&layouts==2&&updates==2);
    for(unsigned i=0;i<6000;i++){
        SurfaceBinding s={.width=1u<<(i%10),.height=1u<<((i/10)%9),.vram_addr=64+i*4,.swizzle=true,.fmt={4},.host_fmt={i%2?VK_FORMAT_D24_UNORM_S8_UINT:VK_FORMAT_D32_SFLOAT_S8_UINT}};
        d.pgraph.scale=1+i%3;unsigned w=s.width*d.pgraph.scale,h=s.height*d.pgraph.scale;
        assert(pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));
        pgraph_vk_pack_depth_swizzled(&d.pgraph,&s,H(VkCommandBuffer,8));
        assert(last_push[0]==w&&last_push[1]==s.width&&last_push[2]==ROUND_UP((uint64_t)w*h*4,256)/4&&last_push[3]==s.width*s.height&&last_push[4]==s.vram_addr/4);
        assert(bound_pipeline==H(VkPipeline,s.host_fmt.vk_format==VK_FORMAT_D32_SFLOAT_S8_UINT?101:100));
        s.swizzle=false;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));s.swizzle=true;
        s.fmt.bytes_per_pixel=2;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));s.fmt.bytes_per_pixel=4;
        s.host_fmt.vk_format=VK_FORMAT_D16_UNORM;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));
    }
    assert(dispatches==6000&&modules==2&&pipelines==2&&sets==1&&layouts==2&&updates==2);
    SurfaceBinding s={.width=128,.height=64,.vram_addr=64,.swizzle=true,.fmt={4},.host_fmt={VK_FORMAT_D24_UNORM_S8_UINT}};d.pgraph.scale=1;
    state.unswizzle.depth_set=0;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));state.unswizzle.depth_set=H(VkDescriptorSet,12);
    state.storage_buffers[0].buffer_size=40000;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));
    state.storage_buffers[0].buffer_size=256<<20;state.storage_buffers[0].buffer=H(VkBuffer,20);state.storage_buffers[1].buffer=H(VkBuffer,21);pgraph_vk_unswizzle_packed_buffer_changed(&d.pgraph);assert(updates==4);
    state.device_props.limits.maxStorageBufferRange=40000;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));state.device_props.limits.maxStorageBufferRange=128<<20;
    s.vram_addr=ram-4;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));s.vram_addr=65;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));s.vram_addr=64;s.width=3;assert(!pgraph_vk_pack_depth_swizzled_possible(&d.pgraph,&s));
    pgraph_vk_finalize_unswizzle(&d.pgraph);assert(dropped_pipelines==2&&dropped_layouts==1&&dropped_sets==1&&dropped_pools==1);assert(!state.unswizzle.depth_set&&!state.unswizzle.ready);
    puts("Fused depth host: 6000 allocation-free dispatches; startup resources, format/range/plane/bounds guards, capped descriptor refresh and teardown passed");
}
