/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <volk.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
/* GPU HELPERS */
int main(int argc,char **argv)
{
    assert(argc>=3&&argc%2==1);gpu_init();unsigned count=0;
    Buffer input=make_buffer(64,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Buffer output=make_buffer(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    VkDescriptorSetLayoutBinding bindings[]={
        {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
    VkDescriptorSetLayout dsl;CHECK(vkCreateDescriptorSetLayout(device,&di,NULL,&dsl));
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&dsl};
    VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&li,NULL,&layout));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool dp;CHECK(vkCreateDescriptorPool(device,&pi,NULL,&dp));
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&dsl};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&ai,&set));
    VkDescriptorBufferInfo buffers[]={{input.buffer,0,64},{output.buffer,0,16}};
    VkWriteDescriptorSet writes[2];
    for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pBufferInfo=&buffers[i]};
    vkUpdateDescriptorSets(device,2,writes,0,NULL);
    for(int file=1;file<argc;file+=2){
        FILE *f=fopen(argv[file],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
        char *text=calloc(n+1,1);assert(text&&fread(text,1,n,f)==(size_t)n);fclose(f);
        GlslcResult code={0};GlslcOptions options={0};
        if(!glslc_compile(GLSLC_STAGE_COMPUTE,text,&options,&code)){fprintf(stderr,"%s: %s\n",argv[file],code.error);return 1;}free(text);
        char dump[4096];snprintf(dump,sizeof(dump),"%s.spv",argv[file]);FILE *spv=fopen(dump,"wb");assert(spv);fwrite(code.words,4,code.num_words,spv);fclose(spv);
        VkShaderModule module=gpu_module(code.words,code.num_words);free(code.words);
        VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
        VkPipeline pipeline;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
        f=fopen(argv[file+1],"rb");assert(f);float values[20];
        while(fread(values,sizeof(values),1,f)==1){
            memcpy(input.mapped,values,64);begin();vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);vkCmdDispatch(command,1,1,1);
            host_barrier(output,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);submit();
            float *actual=output.mapped;
            for(unsigned k=0;k<4;k++)if(!(fabsf(actual[k]-values[16+k])<=0.00005f*fmaxf(1,fabsf(values[16+k])))){
                fprintf(stderr,"%s case %u lane %u: %.9g != %.9g\n",argv[file],count,k,actual[k],values[16+k]);return 1;
            }
            count++;
        }
        fclose(f);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,module,NULL);
    }
    drop_buffer(input);drop_buffer(output);vkDestroyDescriptorPool(device,dp,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,dsl,NULL);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}
    vkDestroyInstance(instance,NULL);assert(!errors);printf("PASS: %u production point/fog expressions via Vulkan SPIR-V\n",count);return 0;
}
