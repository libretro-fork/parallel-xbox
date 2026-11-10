/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_VK_S3TC_PIPELINE_JOB_H
#define PGRAPH_VK_S3TC_PIPELINE_JOB_H
#include "hw/xbox/nv2a/pgraph/spv/utility.h"

/* Fits the existing native job key union. No guest data or caller pointers.
 * Device/cache and any existing layout belong to the renderer until quiesce.
 * Created handles belong to this slot until release publication and adoption. */
typedef struct S3tcPipelineJob {
    VkDescriptorSetLayout created_set_layout;
    unsigned volume;
    bool spirv_1_6, owns_layout, retired;
} S3tcPipelineJob;

static void s3tc_pipeline_job_create(VkDevice device, VkPipelineCache cache,
    VkPipelineLayout *layout, S3tcPipelineJob *job, VkPipeline *result)
{
    assert(job->volume < 2 && job->owns_layout == (*layout == VK_NULL_HANDLE));
    if (job->owns_layout) {
        VkDescriptorSetLayoutBinding bindings[2] = {
            { .binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT },
            { .binding=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT },
        };
        VkDescriptorSetLayoutCreateInfo si = {
            .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount=2, .pBindings=bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device,&si,NULL,&job->created_set_layout));
        VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT,0,9*sizeof(uint32_t) };
        VkPipelineLayoutCreateInfo li = {
            .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount=1, .pSetLayouts=&job->created_set_layout,
            .pushConstantRangeCount=1, .pPushConstantRanges=&push,
        };
        VK_CHECK(vkCreatePipelineLayout(device,&li,NULL,layout));
    }
    SpvStaticModule words=pgraph_spv_utility(job->volume ?
        SPV_UTILITY_S3TC_VOLUME : SPV_UTILITY_S3TC_ARRAY,job->spirv_1_6);
    VkShaderModuleCreateInfo shader_info = {
        .sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=words.num_words*sizeof(uint32_t), .pCode=words.words,
    };
    VkShaderModule module;
    /* Immutable core-image word lend ends at this synchronous native call. */
    VK_CHECK(vkCreateShaderModule(device,&shader_info,NULL,&module));
    VkComputePipelineCreateInfo pi = {
        .sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout=*layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage=VK_SHADER_STAGE_COMPUTE_BIT, .module=module,.pName="main"},
    };
    VK_CHECK(vkCreateComputePipelines(device,cache,1,&pi,NULL,result));
    vkDestroyShaderModule(device,module,NULL);
}
#endif
