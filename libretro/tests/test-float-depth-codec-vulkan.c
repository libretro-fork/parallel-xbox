/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only exhaustive GPU codec check; no emulator diagnostics. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) assert((x)==VK_SUCCESS)
int main(int argc,char **argv) {
 assert(argc==2);
 VkInstance instance;VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
 CHECK(vkCreateInstance(&ici,NULL,&instance));
 uint32_t count=0;CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));assert(count);
 VkPhysicalDevice *devices=calloc(count,sizeof(*devices));CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));VkPhysicalDevice physical=devices[0];free(devices);
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);VkQueueFamilyProperties *families=calloc(count,sizeof(*families));vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
 uint32_t family=0;while(family<count&&!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;assert(family<count);free(families);
 float priority=1;VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};VkDevice device;CHECK(vkCreateDevice(physical,&dci,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 VkBuffer buffer;VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=65536*4,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};CHECK(vkCreateBuffer(device,&bci,NULL,&buffer));
 VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);uint32_t type=0;
 for(;type<memory.memoryTypeCount;type++)if((requirements.memoryTypeBits&(1u<<type))&&(memory.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
 assert(type<memory.memoryTypeCount);VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=type};VkDeviceMemory allocation;CHECK(vkAllocateMemory(device,&mai,NULL,&allocation));CHECK(vkBindBufferMemory(device,buffer,allocation,0));float *mapped;CHECK(vkMapMemory(device,allocation,0,VK_WHOLE_SIZE,0,(void**)&mapped));
 VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&slci,NULL,&set_layout));
 VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};VkDescriptorPool pool;CHECK(vkCreateDescriptorPool(device,&dpci,NULL,&pool));VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&dsai,&set));
 VkDescriptorBufferInfo dbi={.buffer=buffer,.range=65536*4};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&dbi};vkUpdateDescriptorSets(device,1,&write,0,NULL);
 VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&plci,NULL,&layout));
 FILE *file=fopen(argv[1],"rb");assert(file);assert(!fseek(file,0,SEEK_END));long bytes=ftell(file);assert(bytes>0&&!(bytes%4));rewind(file);uint32_t *words=malloc(bytes);assert(words&&fread(words,1,bytes,file)==(size_t)bytes);fclose(file);
 VkShaderModuleCreateInfo smci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=words};VkShaderModule module;CHECK(vkCreateShaderModule(device,&smci,NULL,&module));free(words);
 VkComputePipelineCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};VkPipeline pipeline;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cpci,NULL,&pipeline));
 VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool commands;CHECK(vkCreateCommandPool(device,&pci,NULL,&commands));VkCommandBufferAllocateInfo cbai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;CHECK(vkAllocateCommandBuffers(device,&cbai,&command));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(command,&begin));vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);vkCmdDispatch(command,256,1,1);
 VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);CHECK(vkEndCommandBuffer(command));VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fci,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,60000000000ull));
 for(unsigned code=0;code<65536;code++){float expected=code?ldexpf(1.0f+(code&4095)/4096.0f,(int)(code>>12)-7):0;assert(!memcmp(mapped+code,&expected,4));}
 printf("PASS: %s: all 65,536 F16 codes recovered and decoded exactly on GPU\n",props.deviceName);
 vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,commands,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,module,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkUnmapMemory(device,allocation);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,allocation,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);return 0;
}
