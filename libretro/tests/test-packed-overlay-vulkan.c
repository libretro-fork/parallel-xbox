/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define CHECK VK_CHECK
#define DIV_ROUND_UP(x,n) (((x)+(n)-1)/(n))
#define PGRAPH_VK_AUX_SLOTS 8
#define BUFFER_STAGING_SRC 0
#define BUFFER_UNSWIZZLE_OUT 1
#define PGRAPH_VK_RING_UNSWIZZLE 1
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
static unsigned memory_type(unsigned bits, VkMemoryPropertyFlags flags)
{
    for (unsigned i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((bits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    abort();
}
typedef struct { VkBuffer handle; VkDeviceMemory memory; uint8_t *mapped; } Buffer;
static Buffer buffer(size_t bytes)
{
    Buffer b = {0};
    VkBufferCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    CHECK(vkCreateBuffer(device, &ci, NULL, &b.handle));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, b.handle, &req);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    CHECK(vkAllocateMemory(device, &ai, NULL, &b.memory));
    CHECK(vkBindBufferMemory(device, b.handle, b.memory, 0));
    CHECK(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, (void **)&b.mapped));
    return b;
}
static void drop_buffer(Buffer b)
{
    vkUnmapMemory(device, b.memory); vkDestroyBuffer(device, b.handle, NULL);
    vkFreeMemory(device, b.memory, NULL);
}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)type; (void)user;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        errors++; fprintf(stderr, "%s\n", data->pMessage);
    }
    return VK_FALSE;
}

typedef struct { VkBuffer buffer; VkDeviceSize buffer_size; uint8_t *mapped; } StorageBuffer;
typedef struct {
 VkDevice device; VkPhysicalDevice physical_device; VkPhysicalDeviceProperties device_props; VkPipelineCache vk_pipeline_cache;
 VkBuffer vram_buffer; StorageBuffer storage_buffers[2];
 unsigned aux_slot; bool in_aux_command_buffer; struct {bool pending;} aux[PGRAPH_VK_AUX_SLOTS];
 struct { struct { VkImage image; VkImageView image_view; bool storage; struct { VkDescriptorPool pool; VkDescriptorSetLayout set_layout;
 VkDescriptorSet sets[PGRAPH_VK_AUX_SLOTS]; VkPipelineLayout layout; VkPipeline pipeline; VkBuffer source[PGRAPH_VK_AUX_SLOTS]; VkImageView output[PGRAPH_VK_AUX_SLOTS];
 } convert; } pvideo; } display;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state; size_t src,dst,written;bool reject_ring;} PGRAPHState;
typedef struct {void *vram_ptr;size_t *vram;} NV2AState;
static NV2AState guest_state;
#define container_of(ptr,type,field) ((void)(ptr), &guest_state)
static size_t memory_region_size(size_t *size){return *size;}
static unsigned claims;
static VkDeviceSize claim_offset,claim_size;
static void lr_gpu_ram_claim(uintptr_t cmd,VkDeviceSize offset,size_t size){assert(cmd);claims++;claim_offset=offset;claim_size=size;}
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg,size_t size,size_t align,uint8_t **out)
{assert(align==16&&pg->src+size<=pg->vk_renderer_state->storage_buffers[0].buffer_size);pg->written+=size;size_t offset=pg->src;pg->src=(pg->src+size+15)&~15u;*out=pg->vk_renderer_state->storage_buffers[0].mapped+offset;return offset;}
static void pgraph_vk_staging_flush(PGRAPHState *pg,VkDeviceSize offset,size_t size){(void)pg;(void)offset;(void)size;}
static void pgraph_vk_wait_for_aux(PGRAPHState *pg){(void)pg;/* Test submissions are fence-waited below. */}
static VkShaderModule pgraph_vk_create_fixed_shader_module_from_glsl(PGRAPHVkState *r,VkShaderStageFlagBits stage,const char *source)
{
 assert(stage==VK_SHADER_STAGE_COMPUTE_BIT);GlslcResult result;GlslcOptions opts={0};
 if(!glslc_compile(GLSLC_STAGE_COMPUTE,source,&opts,&result)){fprintf(stderr,"line %d: %s\n",result.error_line,result.error);abort();}
 VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=result.num_words*4,.pCode=result.words};
 VkShaderModule module;CHECK(vkCreateShaderModule(r->device,&ci,NULL,&module));free(result.words);return module;
}

static unsigned dispatch_expected, dispatch_calls;
static void tracked_dispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z)
{
 assert(x==dispatch_expected && y==1 && z==1);dispatch_calls++;
 vkCmdDispatch(cmd,x,y,z);
}
#define vkCmdDispatch tracked_dispatch
/* PRODUCTION */
/* REFERENCE */
int main(void)
{
    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char *extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME };
    VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features = { .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 1, .pEnabledValidationFeatures = &enabled };
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &validation_features, .pApplicationInfo = &app, .enabledLayerCount = 1,
        .ppEnabledLayerNames = layers, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions };
    VkInstance instance; bool validation_enabled=true;
    VkResult instance_result=vkCreateInstance(&ci,NULL,&instance);
#ifdef OVERLAY_ALLOW_NO_VALIDATION
    if(instance_result==VK_ERROR_LAYER_NOT_PRESENT || instance_result==VK_ERROR_EXTENSION_NOT_PRESENT) {
        ci.pNext=NULL;ci.enabledLayerCount=0;ci.enabledExtensionCount=1;
        validation_enabled=false;instance_result=vkCreateInstance(&ci,NULL,&instance);
    }
#endif
    CHECK(instance_result);
    PFN_vkCreateDebugUtilsMessengerEXT create = (void *)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy = (void *)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT mi = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, .messageType = 7,
        .pfnUserCallback = validation };
    VkDebugUtilsMessengerEXT messenger; CHECK(create(instance, &mi, NULL, &messenger));
    uint32_t n = 1; VkPhysicalDevice gpu; CHECK(vkEnumeratePhysicalDevices(instance, &n, &gpu));
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(gpu, &properties);
    printf("Overlay Vulkan oracle: %s\n", properties.deviceName);
    uint32_t nq = 32; VkQueueFamilyProperties queues[32]; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, queues);
    unsigned family = 0; while (family < nq && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family < nq);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    CHECK(vkCreateDevice(gpu, &di, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family };
    VkCommandPool pool; CHECK(vkCreateCommandPool(device, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ca = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(device, &ca, &cmd));

 PGRAPHVkState r={.in_aux_command_buffer=true,.physical_device=gpu,.device=device,.device_props=properties};PGRAPHState pg={.vk_renderer_state=&r};
 pgraph_vk_pvideo_init(&pg);assert(r.display.pvideo.convert.pipeline);
 Buffer src=buffer(16*1024*1024),dst=buffer(16*1024*1024),ram=buffer(16*1024*1024);
 size_t ram_size=16*1024*1024;guest_state=(NV2AState){ram.mapped,&ram_size};
 r.storage_buffers[0]=(StorageBuffer){src.handle,16*1024*1024,src.mapped};
 r.storage_buffers[1]=(StorageBuffer){dst.handle,16*1024*1024,dst.mapped};
 unsigned widths[]={1,2,3,7,16,63,64,65,640,641,1920};size_t checked=0;unsigned cases=0,direct_cases=0;size_t avoided=0;
 for(unsigned test=0;test<90;test++){
  unsigned w=(test==44||test==84)?1920:widths[test%11],h=(test==44||test==84)?1080:13,pitch=(w+1)/2*4+(test%4)*8;
  size_t bytes=(size_t)w*h*4;
  bool imported=test>=45;
  size_t offset=32+(test%3==1?1:0);
  uint8_t *guest=imported?ram.mapped+offset:malloc((size_t)pitch*h),*expected=malloc(bytes);assert(guest&&expected);
  r.vram_buffer=imported?ram.handle:VK_NULL_HANDLE;
  ram_size=test==72?(size_t)properties.limits.maxStorageBufferRange+1:16*1024*1024;
  bool direct=imported&&offset%4==0&&pitch==(w+1)/2*4&&ram_size<=properties.limits.maxStorageBufferRange;
  /* Direct conversion must not depend on staging capacity. */
  r.storage_buffers[0].buffer_size=direct?128:16*1024*1024;
  claims=0;
  uint32_t random=0x1234+test;
  for(size_t i=0;i<(size_t)pitch*h;i++){random=random*1664525u+1013904223u;guest[i]=random>>24;}
  old_rgba(expected,guest,w,h,pitch);memset(dst.mapped,0xAB,bytes+512);
  pg.src=16+16*(test%8);pg.dst=32+16*(test%8);pg.written=0;
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
  VkImageCreateInfo image_ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
  CHECK(vkCreateImage(device,&image_ci,NULL,&r.display.pvideo.image));
  VkMemoryRequirements req;vkGetImageMemoryRequirements(device,r.display.pvideo.image,&req);
  VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,0)};
  VkDeviceMemory image_memory;CHECK(vkAllocateMemory(device,&allocate,NULL,&image_memory));CHECK(vkBindImageMemory(device,r.display.pvideo.image,image_memory,0));
  VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=r.display.pvideo.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  CHECK(vkCreateImageView(device,&view,NULL,&r.display.pvideo.image_view));r.display.pvideo.storage=true;
  dispatch_expected=DIV_ROUND_UP(((w+1)/2)*h,64);unsigned before_dispatch=dispatch_calls;
  /* Exactly enough groups for pairs; a pixel-based preflight would reject. */
  r.device_props.limits.maxComputeWorkGroupCount[0]=dispatch_expected;
  assert(pgraph_vk_pvideo_convert(&pg,cmd,guest,w,h,pitch,true));
  assert(pgraph_vk_pvideo_convert(&pg,cmd,guest,w,h,pitch,false));
  assert(claims==(direct?2u:0u));
  if(direct){assert(!pg.written&&claim_offset==offset&&claim_size==(size_t)pitch*h);direct_cases++;avoided+=2*(size_t)pitch*h;}
  assert(dispatch_calls==before_dispatch+2);
  r.device_props.limits.maxComputeWorkGroupCount[0]=properties.limits.maxComputeWorkGroupCount[0];
  assert(pg.written==(direct?0:2*(size_t)((w+1)/2)*h*4));
  /* Staging snapshots tolerate mutations; direct reads retain the RAM claim. */
  if(!direct)memset(guest,0xE5,(size_t)pitch*h);
  VkImageMemoryBarrier copy_image={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=r.display.pvideo.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&copy_image);
  VkDeviceSize result=16;VkBufferImageCopy copy={.bufferOffset=result,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
  vkCmdCopyImageToBuffer(cmd,r.display.pvideo.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst.handle,1,&copy);
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=dst.handle,.offset=result,.size=bytes};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
  CHECK(vkEndCommandBuffer(cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));
  if(memcmp(dst.mapped+result,expected,bytes)){fprintf(stderr,"mismatch test=%u direct=%u width=%u pitch=%u slot=%u offset=%zu\n",test,direct,w,pitch,r.aux_slot,offset);abort();}for(unsigned i=0;i<16;i++){assert(dst.mapped[result-16+i]==0xAB);assert(dst.mapped[result+bytes+i]==0xAB);}
  memset(r.display.pvideo.convert.output,0,sizeof(r.display.pvideo.convert.output));
  vkDestroyImageView(device,r.display.pvideo.image_view,NULL);vkDestroyImage(device,r.display.pvideo.image,NULL);vkFreeMemory(device,image_memory,NULL);
  vkDestroyFence(device,fence,NULL);CHECK(vkResetCommandBuffer(cmd,0));if(!imported)free(guest);free(expected);checked+=(size_t)w*h;cases++;
  if(test==20){/* Drained buffer growth refreshes the immutable descriptor bindings. */
   memset(r.display.pvideo.convert.source,0,sizeof(r.display.pvideo.convert.source));drop_buffer(src);src=buffer(16*1024*1024);r.storage_buffers[0].buffer=src.handle;r.storage_buffers[0].mapped=src.mapped;
  }
 }
 r.storage_buffers[0].buffer_size=16*1024*1024;r.vram_buffer=VK_NULL_HANDLE;pg.written=0;r.display.pvideo.storage=false;assert(!pgraph_vk_pvideo_convert(&pg,cmd,NULL,64,64,128,true));assert(!pg.written);r.display.pvideo.storage=true;
 r.device_props.limits.maxStorageBufferRange=1024;assert(!pgraph_vk_pvideo_convert(&pg,cmd,NULL,64,64,128,true));assert(!pg.written);r.device_props=properties;
 assert(!pgraph_vk_pvideo_convert(&pg,cmd,NULL,0,1,0,true));assert(!pg.written);
 r.device_props.limits.maxComputeWorkGroupCount[0]=0;assert(!pgraph_vk_pvideo_convert(&pg,cmd,NULL,64,64,128,true));assert(!pg.written);r.device_props=properties;
 pgraph_vk_pvideo_finalize(&pg);assert(!r.display.pvideo.convert.pipeline);
 drop_buffer(ram);drop_buffer(src);drop_buffer(dst);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
 printf("direct overlay cases=%u avoided_staging_bytes=%zu\n",direct_cases,avoided);
 printf("%u production conversion/descriptor/barrier cases, %zu pixels byte-exact; fallback preflight PASS; validation_errors=%u validation_enabled=%u\n",cases,checked,errors,validation_enabled);return errors?1:0;
}
