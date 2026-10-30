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
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
typedef struct { uint8_t *arena; size_t head, capacity; } PGRAPHState;
typedef struct { uint8_t *vram_ptr; PGRAPHState pgraph; } NV2AState;
typedef struct {
    unsigned color_format, width, height, pitch, levels;
    bool cubemap;
} TextureShape;
typedef struct {
    unsigned width, height, depth, row_length;
    VkDeviceSize offset;
    size_t size;
    enum { LEVEL_STAGING, LEVEL_VRAM, LEVEL_UNSWIZZLED } source;
} TextureLevel;
typedef struct {
    bool swizzle, color;
    unsigned pitch, width, height;
    struct { unsigned host_bytes_per_pixel; } host_fmt;
} SurfaceBinding;
typedef struct { VkFormat vk_format; } VkColorFormatInfo;
static const VkColorFormatInfo kelvin_color_format_vk_map[256] = {
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5] = { ORACLE_FORMAT }
};
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg, size_t size,
                                           size_t alignment, uint8_t **ptr)
{
    size_t offset = (pg->head + alignment - 1) & ~(alignment - 1);
    assert(offset + size <= pg->capacity);
    pg->head = offset + size; *ptr = pg->arena + offset;
    return offset;
}
static bool pgraph_vk_unswizzle(PGRAPHState *pg, VkCommandBuffer cmd,
    uintptr_t address, unsigned w, unsigned h, unsigned d, unsigned bpp,
    uintptr_t palette, VkDeviceSize *out)
{
    (void)pg; (void)cmd; (void)address; (void)w; (void)h; (void)d;
    (void)bpp; (void)palette; (void)out; return false;
}
/* Fixture inputs are already linear, isolating host conversion/staging. */
static void unswizzle_rect(const uint8_t *src, unsigned w, unsigned h,
                           uint8_t *dst, size_t pitch, unsigned bpp)
{ (void)w; (void)bpp; memcpy(dst, src, pitch * h); }
static void unswizzle_box(const uint8_t *src, unsigned w, unsigned h, unsigned d,
                          uint8_t *dst, size_t pitch, size_t slice, unsigned bpp)
{ (void)w; (void)h; (void)pitch; (void)bpp; memcpy(dst, src, slice * d); }
static void convert_yuy2_to_rgb(const uint8_t *line, unsigned ix,
                               uint8_t *r, uint8_t *g, uint8_t *b)
{ (void)line; (void)ix; (void)r; (void)g; (void)b; abort(); }
#define convert_uyvy_to_rgb convert_yuy2_to_rgb
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
    VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(gpu, ORACLE_FORMAT, &fp);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    assert((fp.optimalTilingFeatures & required) == required);
    printf("%s: format=%d optimal_features=%x\n", properties.deviceName, ORACLE_FORMAT, fp.optimalTilingFeatures);
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
    TextureShape shape = { .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5, .levels = 1 };
    SurfaceBinding surface = { .color = true, .width = 8, .height = 8, .pitch = 16,
        .host_fmt.host_bytes_per_pixel = 4 };
    shape.width = shape.height = 8; shape.pitch = 16;
    assert(!check_surface_to_texture_compatiblity(&surface, &shape));
    surface.color = false;
    assert(!check_surface_to_texture_compatiblity(&surface, &shape));
    assert(vk_format_texel_size(ORACLE_FORMAT) == 4);
    uint8_t guard[16]; memset(guard, 0xa5, sizeof(guard));
    expand_rgb_snorm_alpha(guard, 0);
    for (unsigned i = 0; i < sizeof(guard); i++) assert(guard[i] == 0xa5);
    const unsigned sizes[] = { 1, 2, 17, 65536 };
    for (unsigned test = 0; test < 4; test++) {
        unsigned count = sizes[test], width = count == 65536 ? 256 : count, height = count / width;
        uint16_t *guest = malloc(count * 2); uint8_t *rgb = malloc(count * 3);
        assert(guest && rgb);
        for (unsigned i = 0; i < count; i++) guest[i] = i;
        pgraph_convert_texture_data_into(shape, (uint8_t *)guest, NULL, width, height, 1, width * 2, count * 2, rgb);
        Buffer upload = buffer(count * 6 + 128), output = buffer(count * 4);
        memset(upload.mapped, 0xa5, count * 6 + 128);
        NV2AState d = { .vram_ptr = (uint8_t *)guest,
            .pgraph = { upload.mapped, 32, count * 6 + 128 } };
        TextureLevel level;
        stage_swizzled(&d.pgraph, cmd, &level, &shape, (uint8_t *)guest, NULL, width, height, 1, 2);
        assert(level.source == LEVEL_STAGING && level.size == count * 4 && level.offset % 4 == 0);
        assert(level.width == width && level.height == height && level.depth == 1 && level.row_length == 0);
        for (unsigned i = 0; i < count; i++) {
            assert(!memcmp(upload.mapped + level.offset + i * 4, rgb + i * 3, 3));
            assert(upload.mapped[level.offset + i * 4 + 3] == 127);
        }
        for (unsigned i = 0; i < 16; i++) assert(upload.mapped[level.offset + level.size + i] == 0xa5);
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = ORACLE_FORMAT, .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1,
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
        VkBufferImageCopy copy = { .bufferOffset = level.offset,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {width, height, 1} };
        vkCmdCopyBufferToImage(cmd, upload.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
        copy.bufferOffset = 0;
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
        assert(!memcmp(output.mapped, upload.mapped + level.offset, count * 4));
        printf("texels=%u: staged RGB unchanged, alpha=127, GPU bytes exact\n", count);
        vkDestroyFence(device, fence, NULL); vkDestroyImage(device, image, NULL); vkFreeMemory(device, memory, NULL);
        drop_buffer(upload); drop_buffer(output); free(guest); free(rgb);
        CHECK(vkResetCommandBuffer(cmd, 0));
    }
    vkDestroyCommandPool(device, pool, NULL); vkDestroyDevice(device, NULL);
    destroy(instance, messenger, NULL); vkDestroyInstance(instance, NULL);
    printf("validation_errors=%u\n", errors);
    return errors ? 1 : 0;
}
