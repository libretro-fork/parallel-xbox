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
    printf("Vulkan cube-copy oracle: %s\n", properties.deviceName);
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
    const struct { VkFormat format; unsigned unit_width, unit_bytes; } formats[] = {
        { VK_FORMAT_R8_UNORM, 1, 1 }, { VK_FORMAT_R8G8_UNORM, 1, 2 },
        { VK_FORMAT_R5G6B5_UNORM_PACK16, 1, 2 },
        { VK_FORMAT_R8G8B8A8_UNORM, 1, 4 }, { VK_FORMAT_R8G8B8A8_SNORM, 1, 4 },
        { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 4, 8 },
        { VK_FORMAT_BC2_UNORM_BLOCK, 4, 16 }, { VK_FORMAT_BC3_UNORM_BLOCK, 4, 16 }
    };
    const unsigned layers_to_test[] = { 0, 3, 5 };
    unsigned cases = 0;
    for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
    for (unsigned geometry = 0; geometry < 7; geometry++)
    for (unsigned kind = 0; kind < 3; kind++) {
        unsigned base = geometry < 3 ? 1u << geometry : 8;
        unsigned mip = geometry < 3 ? 0 : geometry - 3;
        unsigned width = MAX(base >> mip, 1u);
        unsigned source_width = MAX(MAX(16u, base * 2) >> mip, 1u);
        unsigned unit = formats[f].unit_width, bytes = formats[f].unit_bytes;
        unsigned source_columns = (source_width + unit - 1) / unit;
        unsigned target_columns = (width + unit - 1) / unit;
        size_t source_bytes = (size_t)source_columns * source_columns * bytes;
        size_t target_bytes = (size_t)target_columns * target_columns * bytes;
        Buffer upload = buffer(source_bytes + 64), output = buffer(target_bytes);
        memset(upload.mapped, 0xa5, source_bytes + 64);
        for (unsigned i = 0; i < source_bytes; i++) upload.mapped[32 + i] = (i * 29 + f * 17 + geometry * 7 + kind * 11) % 251;
        TextureLevel level = { .width = source_width, .height = source_width, .depth = 1,
            .offset = 32, .size = source_bytes, .source = kind };
        crop_cubemap_level(&level, width, width, mip, unit == 4 ? bytes : 0);
        assert(level.offset == 32 && level.size == source_bytes && (unsigned)level.source == kind);
        assert(level.width == width && level.height == width && level.depth == 1);
        unsigned layer = layers_to_test[kind];
        VkBufferImageCopy copy = texture_level_copy(&level, layer, mip);
        assert(copy.bufferOffset == level.offset + level.crop_offset);
        assert(copy.imageSubresource.baseArrayLayer == layer && copy.imageSubresource.mipLevel == mip);
        assert(copy.bufferRowLength == (unit == 4 ? source_columns * 4 : source_width));
        uint8_t *expected = malloc(target_bytes); assert(expected);
        unsigned origin = mip ? 0 : 4;
        for (unsigned y = 0; y < width; y += unit)
        for (unsigned x = 0; x < width; x += unit) {
            memcpy(expected + ((y / unit) * target_columns + x / unit) * bytes,
                upload.mapped + 32 + (((y + origin) / unit) * source_columns + (x + origin) / unit) * bytes, bytes);
        }
        VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(gpu, formats[f].format, &fp);
        VkFormatFeatureFlags required = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        assert((fp.optimalTilingFeatures & required) == required);
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, .imageType = VK_IMAGE_TYPE_2D,
            .format = formats[f].format, .extent = {base, base, 1}, .mipLevels = mip + 1, .arrayLayers = 6,
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
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, layer, 1} };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
        vkCmdCopyBufferToImage(cmd, upload.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
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
        assert(!memcmp(output.mapped, expected, target_bytes));
        vkDestroyFence(device, fence, NULL); vkDestroyImage(device, image, NULL); vkFreeMemory(device, memory, NULL);
        drop_buffer(upload); drop_buffer(output); free(expected);
        CHECK(vkResetCommandBuffer(cmd, 0)); cases++;
    }
    printf("cube-copy cases=%u: bytes exact; mip bounds and full allocation spans preserved\n", cases);
    vkDestroyCommandPool(device, pool, NULL); vkDestroyDevice(device, NULL);
    destroy(instance, messenger, NULL); vkDestroyInstance(instance, NULL);
    printf("validation_errors=%u\n", errors);
    return errors ? 1 : 0;
}
