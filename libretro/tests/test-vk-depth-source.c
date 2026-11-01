/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define CAPACITY (4u*1024u*1024u)
typedef struct {VkBuffer vram_buffer;} PGRAPHVkState;
typedef struct {bool swizzle;uint64_t vram_addr;unsigned width,pitch;struct {unsigned bytes_per_pixel;} fmt;} SurfaceBinding;
static uint8_t *source_bytes,*destination_bytes,*expected,*snapshot;
static unsigned calls,regions_seen;static VkDeviceSize copied,source_size,destination_size;
static VkBuffer source_handle,destination_handle;
static VkCommandBuffer command;
#ifdef TEST_GPU
static VkInstance instance;static VkDevice device;static VkPhysicalDevice gpu;
static VkQueue queue;static VkCommandPool pool;static VkFence fence;
static VkDeviceMemory source_memory,destination_memory;
static VkBuffer make_buffer(VkDeviceMemory *memory,uint8_t **mapped)
{
 VkBuffer b;VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=CAPACITY,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};VK_CHECK(vkCreateBuffer(device,&ci,NULL,&b));
 VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b,&req);VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(gpu,&props);unsigned type=32;
 for(unsigned pass=0;pass<2&&type==32;pass++)for(unsigned i=0;i<props.memoryTypeCount;i++){VkMemoryPropertyFlags need=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|(pass?0:VK_MEMORY_PROPERTY_HOST_CACHED_BIT);if((req.memoryTypeBits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&need)==need){type=i;break;}}
 assert(type<32);VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};VK_CHECK(vkAllocateMemory(device,&ai,NULL,memory));VK_CHECK(vkBindBufferMemory(device,b,*memory,0));VK_CHECK(vkMapMemory(device,*memory,0,VK_WHOLE_SIZE,0,(void**)mapped));return b;
}
static unsigned validation_errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *opaque)
{(void)type;(void)opaque;if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){validation_errors++;fprintf(stderr,"validation: %s\n",data->pMessage);}return VK_FALSE;}
static VkDebugUtilsMessengerEXT messenger;
static void initialize(void)
{
 const char *layer="VK_LAYER_KHRONOS_validation",*extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;bool validation=getenv("DEPTH_SOURCE_NO_VALIDATION")==NULL;
 VkValidationFeatureEnableEXT feature=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;VkValidationFeaturesEXT vf={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&feature};
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.pNext=validation?&vf:NULL,.enabledLayerCount=validation,.ppEnabledLayerNames=&layer,.enabledExtensionCount=validation,.ppEnabledExtensionNames=&extension};VK_CHECK(vkCreateInstance(&ci,NULL,&instance));
 if(validation){VkDebugUtilsMessengerCreateInfoEXT di={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,.pfnUserCallback=debug_message};PFN_vkCreateDebugUtilsMessengerEXT create=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");assert(create);VK_CHECK(create(instance,&di,NULL,&messenger));}
 unsigned count=0;VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));assert(count);VkPhysicalDevice *devices=malloc(count*sizeof(*devices));assert(devices);VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));gpu=devices[0];free(devices);
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);unsigned n=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,NULL);VkQueueFamilyProperties *families=malloc(n*sizeof(*families));assert(families);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families);unsigned family=0;while(family<n&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<n);free(families);float priority=1;
 VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VK_CHECK(vkCreateDevice(gpu,&di,NULL,&device));vkGetDeviceQueue(device,family,0,&queue);
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VK_CHECK(vkCreateCommandPool(device,&pi,NULL,&pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VK_CHECK(vkAllocateCommandBuffers(device,&ca,&command));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK_CHECK(vkCreateFence(device,&fi,NULL,&fence));
 source_handle=make_buffer(&source_memory,&source_bytes);destination_handle=make_buffer(&destination_memory,&destination_bytes);printf("GPU %s; synchronization validation=%u\n",props.deviceName,validation);
}
static void begin(void)
{
 VK_CHECK(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_CHECK(vkBeginCommandBuffer(command,&bi));
 VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,NULL,0,NULL);
}
static void finish(void)
{
 VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
 VK_CHECK(vkEndCommandBuffer(command));VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};VK_CHECK(vkQueueSubmit(queue,1,&si,fence));VK_CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_C(10000000000)));VK_CHECK(vkResetFences(device,1,&fence));
}
static void finalize(void)
{vkUnmapMemory(device,source_memory);vkUnmapMemory(device,destination_memory);vkDestroyBuffer(device,source_handle,NULL);vkDestroyBuffer(device,destination_handle,NULL);vkFreeMemory(device,source_memory,NULL);vkFreeMemory(device,destination_memory,NULL);vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT drop=(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");drop(instance,messenger,NULL);}assert(!validation_errors);vkDestroyInstance(instance,NULL);}
#else
static void initialize(void){source_bytes=malloc(CAPACITY);destination_bytes=malloc(CAPACITY);assert(source_bytes&&destination_bytes);source_handle=(VkBuffer)(uintptr_t)1;destination_handle=(VkBuffer)(uintptr_t)2;}
static void begin(void){}
static void finish(void){}
static void finalize(void){free(source_bytes);free(destination_bytes);}
#endif
static void checked_copy(VkCommandBuffer cmd,VkBuffer src,VkBuffer dst,unsigned n,const VkBufferCopy *regions)
{
 assert(src==source_handle&&dst==destination_handle&&n&&n<=64);calls++;regions_seen+=n;
 for(unsigned i=0;i<n;i++){const VkBufferCopy *p=&regions[i];assert(p->size&&!(p->srcOffset%4)&&!(p->dstOffset%4)&&!(p->size%4));assert(p->srcOffset+p->size<=source_size&&p->dstOffset+p->size<=destination_size);assert(p->dstOffset==copied);copied+=p->size;
#ifndef TEST_GPU
 memcpy(destination_bytes+p->dstOffset,source_bytes+p->srcOffset,p->size);
#endif
 }
#ifdef TEST_GPU
 vkCmdCopyBuffer(cmd,src,dst,n,regions);
#else
 (void)cmd;
#endif
}
#define vkCmdCopyBuffer checked_copy
/* PRODUCTION */
#undef vkCmdCopyBuffer
static void eligibility(void)
{
 PGRAPHVkState r={source_handle};SurfaceBinding s={.vram_addr=4,.pitch=260,.fmt={4}};assert(surface_upload_from_vram(&r,&s));
 for(unsigned width=1;width<=16;width*=4)for(unsigned bpp=1;bpp<=8;bpp*=2)for(unsigned offset=0;offset<32;offset++)for(unsigned pitch=1;pitch<=80;pitch++)for(unsigned swizzle=0;swizzle<2;swizzle++)for(unsigned imported=0;imported<2;imported++){
  r.vram_buffer=imported?source_handle:VK_NULL_HANDLE;s=(SurfaceBinding){.swizzle=swizzle,.vram_addr=offset,.width=width,.pitch=pitch,.fmt={bpp}};bool expected=imported&&!swizzle&&offset%4==0&&offset%bpp==0&&pitch%bpp==0&&pitch>=width*bpp;assert(surface_upload_from_vram(&r,&s)==expected);
 }
}
int main(void)
{
 initialize();eligibility();expected=malloc(CAPACITY);snapshot=malloc(CAPACITY);assert(expected&&snapshot);unsigned widths[]={0,1,2,3,7,16,31,63,64,65,127,640},heights[]={0,1,2,3,63,64,65,127,128,129,480};unsigned cases=0;
 for(unsigned wi=0;wi<ARRAY_SIZE(widths);wi++)for(unsigned hi=0;hi<ARRAY_SIZE(heights);hi++)for(unsigned pad=0;pad<3;pad++)for(unsigned off=0;off<3;off++){
  VkDeviceSize row=widths[wi]*4,pitch=row+pad*28,offset=off*132;unsigned height=heights[hi];source_size=offset+pitch*height+64;destination_size=row*height+64;assert(source_size<CAPACITY&&destination_size<CAPACITY);
  for(size_t i=0;i<source_size;i++)source_bytes[i]=(uint8_t)(i*17+(i>>8)*31);memcpy(snapshot,source_bytes,source_size);memset(destination_bytes,0xa5,destination_size);memset(expected,0xa5,destination_size);
  for(unsigned y=0;y<height;y++)memcpy(expected+y*row,source_bytes+offset+y*pitch,row);
  calls=regions_seen=0;copied=0;begin();copy_depth_source(command,source_handle,destination_handle,offset,pitch,row,height);finish();
  assert(!memcmp(expected,destination_bytes,destination_size)&&!memcmp(snapshot,source_bytes,source_size));assert(copied==row*height);
  assert(calls==(!row||!height?0u:pad?(height+63)/64:1u));assert(regions_seen==(!row||!height?0u:pad?height:1u));cases++;
 }
 free(expected);free(snapshot);finalize();printf("Depth source: %u packed/padded/offset/empty copy cases and 122880 eligibility cases; exact bytes, source/guard preservation, aligned bounded regions PASS\n",cases);return 0;
}
