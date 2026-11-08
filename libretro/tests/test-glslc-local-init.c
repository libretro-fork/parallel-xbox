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

int main(void)
{
    const char *source =
        "#version 450\nlayout(local_size_x=1) in;\n"
        "layout(std430,set=0,binding=0) buffer Out {uint value[12];} result;\n"
        "int persistent=5;\nint bump(){int x=7; x++; return x;}\n"
        "int arrayCall(){int a[2]=int[2](2,3);a[0]++;return a[0]+a[1];}\n"
        "float vectorCall(){vec4 v=vec4(1.0);v.y+=2.0;return v.x+v.y+v.z+v.w;}\n"
        "void main(){\n"
        " int count=0; for(int y=-2;y<=2;y++){for(int x=-2;x<=2;x++){count++;}}\n"
        " result.value[0]=uint(count);\n"
        " int sum=0;for(int i=0;i<7;i++){int local=2;local++;sum+=local;}result.value[1]=uint(sum);\n"
        " float vs=0.0;for(int i=0;i<5;i++){vec4 v=vec4(1.0);v.x+=2.0;vs+=v.x+v.w;}result.value[2]=uint(vs);\n"
        " float as=0.0;for(int i=0;i<5;i++){float a[2]=float[2](2.0,3.0);a[0]+=1.0;as+=a[0]+a[1];}result.value[3]=uint(as);\n"
        " int bs=0;for(int i=0;i<3;i++){bs+=bump();}result.value[4]=uint(bs);\n"
        " for(int i=0;i<3;i++){persistent++;}result.value[5]=uint(persistent);\n"
        " int outer=0;int ws=0;while(outer<4){int inner=0;while(inner<3){ws++;inner++;}outer++;}result.value[6]=uint(ws);\n"
        " int cs=0;for(int i=0;i<6;i++){if(i==2)continue;int n=4; n++;cs+=n;}result.value[7]=uint(cs);\n"
        " int ac=0;for(int i=0;i<4;i++){ac+=arrayCall();}result.value[8]=uint(ac);\n"
        " float vc=0.0;for(int i=0;i<3;i++){vc+=vectorCall();}result.value[9]=uint(vc);\n"
        " int ds=0;int di=0;do{int n=4;n++;ds+=n;di++;}while(di<3);result.value[10]=uint(ds);\n"
        " int ss=0;for(int i=0;i<4;i++){switch(i&1){case 0:{int n=2;n++;ss+=n;break;}default:{int n=6;n--;ss+=n;break;}}}result.value[11]=uint(ss);\n"
        "}\n";
    gpu_init();
    GlslcResult code = {0}; GlslcOptions options = {0};
    if (!glslc_compile(GLSLC_STAGE_COMPUTE,source,&options,&code)) {
        fprintf(stderr,"%s\n",code.error); return 2;
    }
    VkShaderModule module = gpu_module(code.words,code.num_words); glslc_result_free_words(&code);
    Buffer output = make_buffer(48,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    memset(output.mapped,0x99,48);
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&set_layout));
    VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};
    VkDescriptorPool descriptors;CHECK(vkCreateDescriptorPool(device,&pi,NULL,&descriptors));
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptors,.descriptorSetCount=1,.pSetLayouts=&set_layout};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&ai,&set));
    VkDescriptorBufferInfo buffer={output.buffer,0,48};
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
    vkCmdDispatch(command,1,1,1);host_barrier(output,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);submit();
    const uint32_t expected[]={25,21,20,30,24,8,12,25,24,18,15,16};
    unsigned mismatches=0;
    for(unsigned i=0;i<12;i++)if(((uint32_t*)output.mapped)[i]!=expected[i]) {
        fprintf(stderr,"local initializer case %u: %u expected %u\n",i,((uint32_t*)output.mapped)[i],expected[i]);mismatches++;
    }
    vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);
    vkDestroyDescriptorPool(device,descriptors,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    drop_buffer(output);vkDestroyShaderModule(device,module,NULL);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}
    vkDestroyInstance(instance,NULL);
    if(mismatches||errors)return 1;
    printf("GLSL local initialization: 12 GPU cases passed; validation=%u errors=%u\n",validation_enabled,errors);
    return 0;
}
