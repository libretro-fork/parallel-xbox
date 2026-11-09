/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
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
    const char *source = /* SHADER SOURCE */;
    gpu_init();
    GlslcResult code = {0}; GlslcOptions options = {0};
    if (!glslc_compile(GLSLC_STAGE_COMPUTE,source,&options,&code)) {
        fprintf(stderr,"%s\n",code.error); return 2;
    }
    if(argc!=2)return 2;
    FILE *binary=fopen(argv[1],"wb");if(!binary)return 2;
    bool saved=fwrite(code.words,sizeof(*code.words),code.num_words,binary)==code.num_words;
    if(fclose(binary)||!saved)return 2;
    VkShaderModule module = gpu_module(code.words,code.num_words); glslc_result_free_words(&code);
    Buffer output = make_buffer(4096*16*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    memset(output.mapped,0x99,4096*16*4);
    static const uint32_t bits[16]={0,0x80000000u,0x3f800000u,0xbf800000u,0x40000000u,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc12345u,0xffc54321u,0x7f812345u,0xff854321u,0x00800000u,0x80800000u,1u,0x80000001u};
    uint32_t *data=output.mapped;
    for(unsigned i=0;i<4096;i++)for(unsigned l=0;l<4;l++){
        unsigned pair=i>>4;data[i*16+l]=bits[((pair>>4)+l)&15];data[i*16+4+l]=bits[((pair&15)+3*l)&15];
    }
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&set_layout));
    VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};
    VkDescriptorPool descriptors;CHECK(vkCreateDescriptorPool(device,&pi,NULL,&descriptors));
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptors,.descriptorSetCount=1,.pSetLayouts=&set_layout};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&ai,&set));
    VkDescriptorBufferInfo buffer={output.buffer,0,4096*16*4};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,
        .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffer};
    vkUpdateDescriptorSets(device,1,&write,0,NULL);
    VkPipelineLayoutCreateInfo layout_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout};
    VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&layout_info,NULL,&layout));
    VkComputePipelineCreateInfo pipeline_info={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkPipeline pipeline;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
    begin();vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    vkCmdDispatch(command,4096,1,1);host_barrier(output,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);submit();
    const bool product_oracle=/* PRODUCT ORACLE */;
    unsigned mismatches=0,oracles=0;
    for(unsigned i=0;i<4096;i++)for(unsigned l=0;l<4;l++){
        uint32_t a=data[i*16+l],b=data[i*16+4+l],old=data[i*16+8+l],now=data[i*16+12+l];
        bool selected=(i&(1u<<l))!=0;
        bool old_nan=(old&0x7fffffffu)>0x7f800000u,now_nan=(now&0x7fffffffu)>0x7f800000u;
        bool ok=(old==now)||(old_nan&&now_nan);
        if(!selected)ok=ok&&old==0x99999999u&&now==0x99999999u;
        else if(!product_oracle){}
        else if(!(a&0x7fffffffu)||!(b&0x7fffffffu)){ok=ok&&now==0;oracles++;}
        /* Preserve the existing device denormal policy rather than imposing
         * the CPU's policy on either path, including subnormal*Inf/NaN. */
        else if((!(a&0x7f800000u)&&(a&0x7fffffu))||(!(b&0x7f800000u)&&(b&0x7fffffu))){}
        else if((a&0x7fffffffu)>0x7f800000u||(b&0x7fffffffu)>0x7f800000u){ok=ok&&now_nan;oracles++;}
        else if((a&0x7fffffffu)==0x7f800000u||(b&0x7fffffffu)==0x7f800000u){ok=ok&&now==(((a^b)&0x80000000u)|0x7f800000u);oracles++;}
        else {
            /* Subnormal treatment is compared with the existing GPU path;
             * normal finite products additionally use an independent scalar oracle. */
            float x,y,z;memcpy(&x,&a,4);memcpy(&y,&b,4);z=x*y;uint32_t expect;memcpy(&expect,&z,4);
            if((a&0x7f800000u)&&(b&0x7f800000u)&&(expect&0x7f800000u)){ok=ok&&now==expect;oracles++;}
        }
        if(!ok){if(mismatches<8)fprintf(stderr,"/* OP NAME */ case %u lane %u: %08x * %08x: %08x vs %08x\n",i,l,a,b,old,now);mismatches++;}
    }
    vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);
    vkDestroyDescriptorPool(device,descriptors,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    drop_buffer(output);vkDestroyShaderModule(device,module,NULL);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}
    vkDestroyInstance(instance,NULL);
    if(mismatches||errors)return 1;
    printf("NV2A /* OP NAME */: 16384 masked lane comparisons, %u independent oracles passed; validation=%u errors=%u\n",oracles,validation_enabled,errors);
    return 0;
}
