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
/* PRODUCTION */

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
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(gpu, &supported);
    assert(supported.textureCompressionBC);
    VkPhysicalDeviceFeatures features = { .textureCompressionBC = VK_TRUE };
    printf("Vulkan volume block-copy oracle: %s\n", properties.deviceName);
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
    const VkFormat formats[] = {VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK};
    const unsigned widths[] = {1,4,8}, heights[] = {1,4,8}, depths[] = {1,2,4,5};
    unsigned cases = 0, direct = 0; size_t avoided = 0;
    for (unsigned f=0;f<3;f++)
    for (unsigned wi=0;wi<3;wi++)
    for (unsigned hi=0;hi<3;hi++)
    for (unsigned di=0;di<4;di++)
    for (unsigned kind=0;kind<3;kind++) {
        unsigned w=widths[wi], h=heights[hi], depth=depths[di];
        unsigned bs=f?16:8, blocks=((w+3)/4)*((h+3)/4);
        size_t bytes=(size_t)blocks*depth*bs, offset=kind==2?36:32;
        Buffer upload=buffer(bytes+64), staging=buffer(bytes+16), output=buffer(bytes);
        memset(staging.mapped,0x9b,bytes+16);
        uint8_t *expected=malloc(bytes); assert(expected);
        /* Independent coordinate address: four-slice group, XY block,
         * then slice within the group. Expected output is ordinary Z order. */
        for(unsigned z=0;z<depth;z++)
        for(unsigned b=0;b<blocks;b++)
        for(unsigned byte=0;byte<bs;byte++) {
            unsigned group=(z/4)*4, count=MIN(depth-group,4);
            size_t src=((size_t)group*blocks + b*count + z%4)*bs+byte;
            size_t dst=((size_t)z*blocks+b)*bs+byte;
            uint8_t value=(z*53+b*19+byte*7+f*31)&255;
            upload.mapped[offset+src]=value; expected[dst]=value;
        }
        PGRAPHVkState renderer={.vram_buffer=kind==1?VK_NULL_HANDLE:upload.handle};
        size_t limit=offset+bytes; /* valid data ending exactly at VRAM end */
        NV2AState state={.vram_ptr=upload.mapped,.vram=&limit,
            .pgraph={.vk_renderer_state=&renderer,.arena=staging.mapped}};
        TextureLevel level={0};
        stage_blocks_3d(&state.pgraph,&level,upload.mapped+offset,w,h,depth,bs);
        bool eligible=kind==0 && (depth==1 || blocks==1);
        assert((level.source==LEVEL_VRAM)==eligible);
        assert(level.width==w && level.height==h && level.depth==depth && level.size==bytes);
        assert(!level.row_length && !level.crop_offset);
        if(eligible) {
            assert(!state.pgraph.head && level.offset==offset);
            for(size_t i=0;i<bytes+16;i++) assert(staging.mapped[i]==0x9b);
            direct++; avoided+=bytes;
        } else {
            assert(state.pgraph.head==bytes && level.offset==0);
        }
        assert(!memcmp(eligible?upload.mapped+offset:staging.mapped,expected,bytes));
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
    printf("volume cases=%u direct=%u avoided_staging_bytes=%zu; all bytes exact\n",cases,direct,avoided);
    vkDestroyCommandPool(device, pool, NULL); vkDestroyDevice(device, NULL);
    destroy(instance, messenger, NULL); vkDestroyInstance(instance, NULL);
    printf("validation_errors=%u\n", errors);
    return errors ? 1 : 0;
}
