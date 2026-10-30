/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define CHECK(x) assert((x) == VK_SUCCESS)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct { VkBuffer vram_buffer; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; uint8_t *arena; size_t head; } PGRAPHState;
typedef struct { uint8_t *vram_ptr; size_t *vram; PGRAPHState pgraph; } NV2AState;
#define memory_region_size(p) (*(p))
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg, size_t bytes,
                                           size_t align, uint8_t **out)
{
    size_t offset = (pg->head + align-1) & ~(align-1);
    pg->head = offset + bytes; *out = pg->arena + offset;
    return offset;
}
static size_t copy_calls;
static void *record_copy(void *dst, const void *src, size_t bytes)
{
    copy_calls++;
    return memcpy(dst, src, bytes);
}
#define memcpy record_copy
/* PRODUCTION */
#undef memcpy

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
        .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
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
int main(void)
{
    /* Empty ranges must not underflow the trimmed staging span. */
    uint8_t empty_data[16]={0}, empty_arena[16]={0};
    size_t empty_limit=sizeof(empty_data);
    PGRAPHVkState no_vram={0};
    NV2AState empty={.vram_ptr=empty_data,.vram=&empty_limit,
        .pgraph={.vk_renderer_state=&no_vram,.arena=empty_arena}};
    TextureLevel empty_level={0};
    stage_linear_2d(&empty.pgraph,&empty_level,empty_data,3,0,128,1);
    assert(!empty.pgraph.head && !empty_level.size);

    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char *extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME };
    VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features = { .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 1, .pEnabledValidationFeatures = &enabled };
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &validation_features, .pApplicationInfo = &app, .enabledLayerCount = 1,
        .ppEnabledLayerNames = layers, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions };
    VkInstance instance; CHECK(vkCreateInstance(&ci, NULL, &instance));
    PFN_vkCreateDebugUtilsMessengerEXT create = (void *)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy = (void *)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT mi = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, .messageType = 7,
        .pfnUserCallback = validation };
    VkDebugUtilsMessengerEXT messenger; CHECK(create(instance, &mi, NULL, &messenger));
    uint32_t n = 1; VkPhysicalDevice gpu; CHECK(vkEnumeratePhysicalDevices(instance, &n, &gpu));
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(gpu, &properties);
    VkPhysicalDeviceFeatures features = {0};
    printf("Vulkan linear staging-span oracle: %s\n", properties.deviceName);
    uint32_t nq = 32; VkQueueFamilyProperties queues[32]; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, queues);
    unsigned family = 0; while (family < nq && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family < nq);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .pEnabledFeatures = &features };
    CHECK(vkCreateDevice(gpu, &di, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family };
    VkCommandPool pool; CHECK(vkCreateCommandPool(device, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ca = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(device, &ca, &cmd));
    const VkFormat formats[] = {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R5G6B5_UNORM_PACK16};
    const unsigned pixel_bytes[] = {1,2,4,2};
    const unsigned widths[] = {1,3,8}, heights[] = {1,2,8};
    unsigned cases=0,direct=0; size_t omitted=0;
    for(unsigned f=0;f<4;f++)
    for(unsigned wi=0;wi<3;wi++)
    for(unsigned hi=0;hi<3;hi++)
    for(unsigned padding=0;padding<3;padding++)
    for(unsigned kind=0;kind<3;kind++) {
        unsigned w=widths[wi],h=heights[hi],bpp=pixel_bytes[f];
        unsigned pitch=padding==2?128:w*bpp+padding*4;
        unsigned depth=1;
        size_t guest_bytes=(size_t)pitch*h, active=(size_t)pitch*(h-1)+w*bpp;
        size_t bytes=(size_t)w*h*bpp,offset=kind==2?33:32;
        Buffer upload=buffer(guest_bytes+64),staging=buffer(guest_bytes+16),output=buffer(bytes);
        memset(upload.mapped,0xab,guest_bytes+64); memset(staging.mapped,0x9b,guest_bytes+16);
        uint8_t *expected=malloc(bytes); assert(expected);
        for(unsigned y=0;y<h;y++)
        for(unsigned x=0;x<w*bpp;x++) {
            uint8_t value=(y*53+x*17+f*31)&255;
            upload.mapped[offset+y*pitch+x]=value; expected[(y*w*bpp)+x]=value;
        }
        PGRAPHVkState renderer={.vram_buffer=kind==1?VK_NULL_HANDLE:upload.handle};
        size_t limit=offset+guest_bytes;
        NV2AState state={.vram_ptr=upload.mapped,.vram=&limit,
            .pgraph={.vk_renderer_state=&renderer,.arena=staging.mapped}};
        TextureLevel level={0}; copy_calls=0;
        stage_linear_2d(&state.pgraph,&level,upload.mapped+offset,w,h,pitch,bpp);
        bool eligible=kind==0;
        assert((level.source==LEVEL_VRAM)==eligible);
        assert(level.width==w && level.height==h && level.depth==1 && !level.crop_offset);
        assert(level.row_length==pitch/bpp);
        if(eligible) {
            assert(!copy_calls && !state.pgraph.head && level.offset==offset && level.size==guest_bytes);
            direct++;
        } else {
            assert(copy_calls==1 && state.pgraph.head==active && level.offset==0 && level.size==active);
            assert(!memcmp(staging.mapped,upload.mapped+offset,active));
            for(size_t i=active;i<guest_bytes+16;i++) assert(staging.mapped[i]==0x9b);
            omitted+=guest_bytes-active;
        }
        VkBufferImageCopy copy=texture_level_copy(&level,0,0);
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_3D, .format = formats[f],
            .extent = {w,h,depth}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
        VkImage image; CHECK(vkCreateImage(device, &ii, NULL, &image));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(device, image, &req);
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
            .memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        VkDeviceMemory memory; CHECK(vkAllocateMemory(device, &ai, NULL, &memory)); CHECK(vkBindImageMemory(device, image, memory, 0));
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        CHECK(vkBeginCommandBuffer(cmd, &bi));
        VkImageMemoryBarrier ib = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
        vkCmdCopyBufferToImage(cmd, eligible?upload.handle:staging.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
        copy.bufferOffset = 0; copy.bufferRowLength = 0;
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.handle, 1, &copy);
        VkBufferMemoryBarrier bb = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = output.handle, .size = VK_WHOLE_SIZE };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
        CHECK(vkEndCommandBuffer(cmd));
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; VkFence fence;
        CHECK(vkCreateFence(device, &fi, NULL, &fence));
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
        CHECK(vkQueueSubmit(queue, 1, &si, fence)); CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL));
        assert(!memcmp(output.mapped, expected, bytes));
        vkDestroyFence(device, fence, NULL); vkDestroyImage(device, image, NULL); vkFreeMemory(device, memory, NULL);
        drop_buffer(upload); drop_buffer(staging); drop_buffer(output); free(expected);
        CHECK(vkResetCommandBuffer(cmd, 0)); cases++;
    }
    printf("linear cases=%u direct=%u omitted_staging_bytes=%zu; pixels exact\n",cases,direct,omitted);
    vkDestroyCommandPool(device, pool, NULL); vkDestroyDevice(device, NULL);
    destroy(instance, messenger, NULL); vkDestroyInstance(instance, NULL);
    printf("validation_errors=%u\n", errors);
    return errors ? 1 : 0;
}
