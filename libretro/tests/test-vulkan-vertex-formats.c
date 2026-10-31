/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/vk/vertex-format.h"
#include "vertex-format-probe-spv.h"
#define CHECK(x) assert((x)==VK_SUCCESS)
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
static unsigned memory_type(unsigned bits) {
    for(unsigned i=0;i<memory_properties.memoryTypeCount;i++)
        if((bits&(1u<<i)) && (memory_properties.memoryTypes[i].propertyFlags&
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return i;
    abort();
}
typedef struct { VkBuffer handle;VkDeviceMemory memory;uint8_t *mapped; } Buffer;
static Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage) {
    Buffer b={0};VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    CHECK(vkCreateBuffer(device,&ci,NULL,&b.handle));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.handle,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits)};
    CHECK(vkAllocateMemory(device,&ai,NULL,&b.memory));CHECK(vkBindBufferMemory(device,b.handle,b.memory,0));
    CHECK(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,(void **)&b.mapped));return b;
}
static void drop(Buffer b) { vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.handle,NULL);vkFreeMemory(device,b.memory,NULL); }
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { errors++;fprintf(stderr,"%s\n",data->pMessage); }
    return VK_FALSE;
}
int main(void) {
    const char *layer="VK_LAYER_KHRONOS_validation";
    const char *extensions[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
    VkValidationFeatureEnableEXT feature=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT vf={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&feature};
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pNext=&vf,.pApplicationInfo=&app,
        .enabledLayerCount=1,.ppEnabledLayerNames=&layer,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
    PFN_vkCreateDebugUtilsMessengerEXT create=(void *)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void *)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};
    VkDebugUtilsMessengerEXT messenger;CHECK(create(instance,&mi,NULL,&messenger));
    uint32_t n=1;VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));
    vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);
    VkPhysicalDeviceFeatures features;vkGetPhysicalDeviceFeatures(gpu,&features);assert(features.vertexPipelineStoresAndAtomics);
    features=(VkPhysicalDeviceFeatures){.vertexPipelineStoresAndAtomics=VK_TRUE};
    uint32_t nq=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families);
    unsigned family=0;while(family<nq && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family<nq);float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pEnabledFeatures=&features,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    CHECK(vkCreateDevice(gpu,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VkCommandPool pool;CHECK(vkCreateCommandPool(device,&pi,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&ca,&cmd));
    VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(vertex_probe_spv),.pCode=vertex_probe_spv};
    VkShaderModule shader;CHECK(vkCreateShaderModule(device,&smi,NULL,&shader));
    VkDescriptorSetLayoutBinding db={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo dli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&db};
    VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&dli,NULL,&set_layout));
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout};
    VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&pli,NULL,&layout));
    VkDescriptorPoolSize dp={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&dp};
    VkDescriptorPool descriptor_pool;CHECK(vkCreateDescriptorPool(device,&dpi,NULL,&descriptor_pool));
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&dai,&set));
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};
    VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&sub};
    VkRenderPass pass;CHECK(vkCreateRenderPass(device,&ri,NULL,&pass));
    VkFramebufferCreateInfo fbi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=1,.height=1,.layers=1};
    VkFramebuffer framebuffer;CHECK(vkCreateFramebuffer(device,&fbi,NULL,&framebuffer));
    Buffer input=buffer(4096,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),output=buffer(4096,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),indices=buffer(256,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    VkDescriptorBufferInfo info={output.handle,0,VK_WHOLE_SIZE};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};
    vkUpdateDescriptorSets(device,1,&write,0,NULL);
    const VkFormat formats[]={VK_FORMAT_R8G8B8_UNORM,VK_FORMAT_R16G16B16_SNORM,VK_FORMAT_R16_SSCALED,
        VK_FORMAT_R16G16_SSCALED,VK_FORMAT_R16G16B16_SSCALED,VK_FORMAT_R16G16B16A16_SSCALED};
    const int samples[]={0,1,-1,32767,-32768,12345,-23456,256};unsigned cases=0;
    for(unsigned f=0;f<6;f++) for(unsigned interleaved=0;interleaved<2;interleaved++) for(unsigned indexed=0;indexed<2;indexed++) {
        unsigned components=f<2?3:f-1,source_width=f==0?3:components*2;
        enum VertexConvert convert;VkFormat format=pgraph_vk_vertex_substitute_format(formats[f],&convert);
        unsigned width=f==0?4:f==1?8:components*4,stride=width+interleaved*16,source_stride=source_width+3;
        VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(gpu,format,&fp);assert(fp.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
        uint8_t source[2048];memset(source,0xff,sizeof(source));
        const unsigned first=5,end=37;
        float expected[37][4];memset(expected,0,sizeof(expected));
        for(unsigned v=0;v<end;v++) {
            expected[v][3]=1;
            for(unsigned k=0;k<components;k++) {
                if(f==0) { uint8_t value=(uint8_t)(v*17+k*31);source[1+v*source_stride+k]=value;expected[v][k]=value/255.0f; }
                else { int value=samples[(v+k)%8];uint16_t raw=(uint16_t)value;
                    source[1+v*source_stride+k*2]=(uint8_t)raw;source[2+v*source_stride+k*2]=(uint8_t)(raw>>8);
                    expected[v][k]=f==1?fmaxf(-1,value/32767.0f):(float)value; }
            }
        }
        memset(input.mapped,0xcc,4096);memset(output.mapped,0xa5,4096);
        pgraph_vk_convert_vertex_attribute(input.mapped+64+first*stride,source+1+first*source_stride,
            end-first,source_stride,stride,source_width,convert);
        for(unsigned i=0;i<end-first;i++) ((uint32_t *)indices.mapped)[i]=first+(i*7)%(end-first);
        VkVertexInputBindingDescription binding={0,stride,VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attribute={0,0,format,0};
        VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,.vertexAttributeDescriptionCount=1,.pVertexAttributeDescriptions=&attribute};
        VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
        VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.rasterizerDiscardEnable=VK_TRUE,.lineWidth=1};
        VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
        VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=shader,.pName="main"};
        VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=1,.pStages=&stage,
            .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pRasterizationState=&rs,.pMultisampleState=&ms,.layout=layout,.renderPass=pass};
        VkPipeline pipeline;CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,NULL,&pipeline));
        VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&bi));
        VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea.extent={1,1}};
        vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);VkDeviceSize offset=64;
        vkCmdBindVertexBuffers(cmd,0,1,&input.handle,&offset);
        if(indexed) { vkCmdBindIndexBuffer(cmd,indices.handle,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(cmd,end-first,1,0,0,0); }
        else vkCmdDraw(cmd,end-first,1,first,0);
        vkCmdEndRenderPass(cmd);
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.handle,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);CHECK(vkEndCommandBuffer(cmd));
        VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
        CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
        for(unsigned v=first;v<end;v++) for(unsigned k=0;k<4;k++) {
            float value;memcpy(&value,output.mapped+(v*4+k)*4,4);assert(fabsf(value-expected[v][k])<1e-6f);
        }
        for(unsigned b=0;b<first*16;b++) assert(output.mapped[b]==0xa5);
        for(unsigned b=0;b<64+first*stride;b++) assert(input.mapped[b]==0xcc);
        vkDestroyFence(device,fence,NULL);vkDestroyPipeline(device,pipeline,NULL);CHECK(vkResetCommandBuffer(cmd,0));cases++;
    }
    drop(input);drop(output);drop(indices);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);
    vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    vkDestroyShaderModule(device,shader,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
    printf("Vulkan vertex formats: %u converted vertex/indexed fetch cases, normalized/signed/default components passed; validation_errors=%u\n",cases,errors);
    return errors?1:0;
}
