/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
typedef struct {struct {uint16_t uniform_attrs;} vsh;} ShaderState;
typedef struct {
    VkDevice device;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout graphics_pipeline_layouts[NV2A_VERTEXSHADER_ATTRIBUTES+1];
    VkPipelineLayout clear_pipeline_layout;
    bool use_push_constants_for_uniform_attrs;
} PGRAPHVkState;
static uint64_t next_handle,calls,drops,queries;
static unsigned phase;
static PGRAPHVkState *owner;
static bool alive[8192];
static VkResult vkCreatePipelineLayout(VkDevice device,const VkPipelineLayoutCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipelineLayout *out) {
    assert(device==owner->device && !allocator && info->sType==VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO && !info->pNext && !info->flags);
    if(!phase) {
        assert(!info->setLayoutCount && !info->pSetLayouts && !info->pushConstantRangeCount && !info->pPushConstantRanges);
    } else {
        assert(phase==1);unsigned index=owner->use_push_constants_for_uniform_attrs?NV2A_VERTEXSHADER_ATTRIBUTES:0;
        assert(info->setLayoutCount==1 && info->pSetLayouts==&owner->descriptor_set_layout);
        assert(info->pushConstantRangeCount==!!index);
        if(index)assert(info->pPushConstantRanges && info->pPushConstantRanges->size==index*16 && !info->pPushConstantRanges->offset && info->pPushConstantRanges->stageFlags==VK_SHADER_STAGE_VERTEX_BIT);
        else assert(!info->pPushConstantRanges);
    }
    phase++;calls++;assert(++next_handle<ARRAY_SIZE(alive));alive[next_handle]=true;
    *out=(VkPipelineLayout)(uintptr_t)next_handle;return VK_SUCCESS;
}
static void vkDestroyPipelineLayout(VkDevice device,VkPipelineLayout handle,const VkAllocationCallbacks *allocator) {
    unsigned id=(unsigned)(uintptr_t)handle;assert(device==owner->device && !allocator && id && id<ARRAY_SIZE(alive) && alive[id]);alive[id]=false;drops++;
}
/* PRODUCTION */
int main(void) {
    for(unsigned generation=0;generation<16;generation++)for(unsigned push=0;push<2;push++) {
        PGRAPHVkState r={.device=(VkDevice)(uintptr_t)(generation+1),.descriptor_set_layout=(VkDescriptorSetLayout)(uintptr_t)(generation+100),.use_push_constants_for_uniform_attrs=push};owner=&r;phase=0;
        uint64_t before=calls,old_drops=drops;init_pipeline_layouts(&r);assert(calls-before==2);
        assert(r.clear_pipeline_layout && r.graphics_pipeline_layouts[0]!=r.clear_pipeline_layout);
        unsigned live_index=push?NV2A_VERTEXSHADER_ATTRIBUTES:0;
        for(unsigned i=0;i<ARRAY_SIZE(r.graphics_pipeline_layouts);i++)
            assert(!!r.graphics_pipeline_layouts[i]==(i==live_index));
        for(unsigned mask=0;mask<65536;mask++) {
            ShaderState state={.vsh={.uniform_attrs=(uint16_t)mask}};
            unsigned index=push?NV2A_VERTEXSHADER_ATTRIBUTES:0;
            VkPipelineLayout expected=r.graphics_pipeline_layouts[index];
            assert(expected && alive[(uintptr_t)expected]);
            assert(create_pipeline_layout(&r,&state)==expected);
            assert(create_pipeline_layout_size(&r,index*16)==expected);
            assert(calls-before==2 && drops==old_drops);queries+=2;
        }
        finalize_pipeline_layouts(&r);assert(drops-old_drops==2);
        for(unsigned i=0;i<ARRAY_SIZE(r.graphics_pipeline_layouts);i++)assert(!r.graphics_pipeline_layouts[i]);
        assert(!r.clear_pipeline_layout);
    }
    assert(calls==drops);
    printf("Graphics layout lends: %llu compatible-range queries, all 65536 attribute masks, push/no-push devices and 32 renderer generations PASS; no lookup creation/copy/allocation; %zu owner bytes\n",(unsigned long long)queries,sizeof(owner->graphics_pipeline_layouts)+sizeof(owner->clear_pipeline_layout));
}
