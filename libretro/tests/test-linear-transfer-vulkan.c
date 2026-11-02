/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real-device oracle for the production GPU-only byte transfer. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "volk.h"
#include "hw/xbox/nv2a/nv2a_regs.h"

#define CHECK(call) do { VkResult check_result = (call); if (check_result != VK_SUCCESS) { \
    fprintf(stderr, "%s: Vulkan error %d\n", #call, check_result); exit(1); } } while (0)
#define BUFFER_COMPUTE_DST 0
#define NV2A_PROF_SURF_TO_TEX_REINTERPRET 0
typedef struct { unsigned color_format, width, height; } TextureShape;
typedef struct {
    struct { VkFormat vk_format; } host_fmt;
    unsigned width, height;
    VkImage image;
    int draw_time;
} SurfaceBinding;
typedef struct {
    struct { TextureShape state; } key;
    VkImage image;
    VkImageLayout current_layout;
    int draw_time;
} TextureBinding;
typedef struct { struct { VkBuffer buffer; } storage_buffers[1]; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; unsigned surface_scale_factor; } PGRAPHState;
typedef struct { VkFormat vk_format; VkComponentMapping component_map; } VkColorFormatInfo;
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkCommandBuffer command;
static VkImage scratch;
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r, unsigned f)
{
    (void)r;
    /* FORMAT_MAP */
    return kelvin_color_format_vk_map[f];
}
static VkImage native_get_scratch(PGRAPHState *p, VkFormat f, unsigned w, unsigned h)
{ (void)p; (void)f; (void)w; (void)h; return scratch; }
static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *p)
{ (void)p; return command; }
static void pgraph_vk_end_nondraw_commands(PGRAPHState *p, VkCommandBuffer c)
{ (void)p; (void)c; }
static VkImageLayout pgraph_vk_surface_base_layout(SurfaceBinding *s)
{ (void)s; return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
static void pgraph_vk_surface_left_shader_read(PGRAPHState *p, SurfaceBinding *s)
{ (void)p; (void)s; }
static void nv2a_profile_inc_counter(int c) { (void)c; }
static void pgraph_vk_transition_image_layout(PGRAPHState *p, VkCommandBuffer c,
    VkImage image, VkFormat format, VkImageLayout old, VkImageLayout next)
{
    (void)p; (void)format;
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = old == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = old, .newLayout = next,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}
static unsigned vk_format_texel_size(VkFormat format)
{
    assert(format==VK_FORMAT_B8G8R8A8_UNORM||format==VK_FORMAT_R5G6B5_UNORM_PACK16);
    return format==VK_FORMAT_B8G8R8A8_UNORM?4:2;
}
/* PRODUCTION */
static VkDeviceMemory allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags)
{
    unsigned type;
    for (type = 0; type < memory_properties.memoryTypeCount; type++)
        if ((requirements.memoryTypeBits & (1u << type)) &&
            (memory_properties.memoryTypes[type].propertyFlags & flags) == flags) break;
    assert(type < memory_properties.memoryTypeCount);
    VkMemoryAllocateInfo info = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type };
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &info, NULL, &memory));
    return memory;
}
static VkBuffer buffer(unsigned size, VkMemoryPropertyFlags flags, VkDeviceMemory *memory)
{
    VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    VkBuffer result;
    CHECK(vkCreateBuffer(device, &info, NULL, &result));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, result, &requirements);
    *memory = allocate(requirements, flags);
    CHECK(vkBindBufferMemory(device, result, *memory, 0));
    return result;
}
static VkImage image(VkFormat format, unsigned width, unsigned height,
                     VkImageUsageFlags usage, VkDeviceMemory *memory)
{
    VkImageCreateInfo info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = format, .extent = { width, height, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    VkImage result;
    CHECK(vkCreateImage(device, &info, NULL, &result));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, result, &requirements);
    *memory = allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkBindImageMemory(device, result, *memory, 0));
    return result;
}
static unsigned char pattern(unsigned x, unsigned y, unsigned byte, unsigned generation)
{ return x * 37 + y * 71 + byte * 53 + generation * 109; }
int main(void)
{
    CHECK(volkInitialize());
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "linear byte transfer oracle", .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    CHECK(vkCreateInstance(&ii, NULL, &instance));
    volkLoadInstance(instance);
    unsigned count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    assert(count);
    VkPhysicalDevice *gpus = calloc(count, sizeof(*gpus));
    CHECK(vkEnumeratePhysicalDevices(instance, &count, gpus));
    VkPhysicalDevice gpu = gpus[0];
    free(gpus);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(gpu, &properties);
    printf("linear-byte GPU: %s\n", properties.deviceName);
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families);
    unsigned family;
    for (family = 0; family < count; family++) if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    assert(family < count);
    free(families);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    CHECK(vkCreateDevice(gpu, &di, NULL, &device));
    volkLoadDevice(device);
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    CHECK(vkAllocateCommandBuffers(device, &ai, &command));
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK(vkCreateFence(device, &fi, NULL, &fence));
    VkDeviceMemory host_memory, transfer_memory, scratch_memory;
    VkBuffer host = buffer(131072, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &host_memory);
    unsigned char *mapped;
    CHECK(vkMapMemory(device, host_memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
    PGRAPHVkState renderer = { .storage_buffers = {{ buffer(2048,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &transfer_memory) }} };
    const unsigned formats[] = { NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8 };
    unsigned cases = 0;
    for (unsigned source_bpp = 2; source_bpp <= 4; source_bpp *= 2)
    for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
    for (unsigned scale = 1; scale <= 4; scale *= 2)
    for (unsigned pitch = 32 * source_bpp; pitch <= 64 * source_bpp; pitch *= 2) {
        PGRAPHState pg = { &renderer, scale };
        VkFormat source_format = source_bpp == 4 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;
        scratch = image(source_format, 32, 16, 0, &scratch_memory);
        VkDeviceMemory source_memory, destination_memory;
        SurfaceBinding source = { .host_fmt = { source_format },
            .width = 32, .height = 16, .draw_time = 1,
            .image = image(source_format, 32 * scale, 16 * scale,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &source_memory) };
        TextureBinding destination = { .key.state = { formats[f], 64, 16 } };
        VkFormat format = texture_vkf(&renderer, formats[f]).vk_format;
        unsigned texture_bpp = format == VK_FORMAT_R8_UNORM ? 1 :
            format == VK_FORMAT_B8G8R8A8_UNORM ? 4 : 2;
        unsigned width = 32 * source_bpp / texture_bpp;
        destination.key.state.width = width;
        destination.image = image(format, width, 16, VK_IMAGE_USAGE_SAMPLED_BIT, &destination_memory);
        for (unsigned generation = 0; generation < 2; generation++) {
            memset(mapped, 0xa7, pitch * 16 * scale * scale);
            for (unsigned y = 0; y < 16 * scale; y++)
            for (unsigned x = 0; x < 32 * scale; x++) for (unsigned b = 0; b < source_bpp; b++)
                mapped[y * pitch * scale + x * source_bpp + b] = pattern(x / scale, y / scale, b, generation);
            memset(mapped + 65536, 0xcd, 2048);
            CHECK(vkResetCommandBuffer(command, 0));
            VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            CHECK(vkBeginCommandBuffer(command, &bi));
            pgraph_vk_transition_image_layout(&pg, command, source.image, source.host_fmt.vk_format,
                generation ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region = { .bufferRowLength = pitch / source_bpp * scale,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageExtent = { 32 * scale, 16 * scale, 1 } };
            vkCmdCopyBufferToImage(command, host, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            pgraph_vk_transition_image_layout(&pg, command, source.image, source.host_fmt.vk_format,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            copy_surface_to_linear_texture(&pg, &source, &destination);
            pgraph_vk_transition_image_layout(&pg, command, destination.image, format,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            region.bufferOffset = 65536; region.bufferRowLength = 0;
            region.imageExtent = (VkExtent3D){ width, 16, 1 };
            vkCmdCopyImageToBuffer(command, destination.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host, 1, &region);
            pgraph_vk_transition_image_layout(&pg, command, destination.image, format,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &mb, 0, NULL, 0, NULL);
            CHECK(vkEndCommandBuffer(command));
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1, .pCommandBuffers = &command };
            CHECK(vkQueueSubmit(queue, 1, &si, fence));
            CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
            CHECK(vkResetFences(device, 1, &fence));
            for (unsigned y = 0; y < 16; y++) for (unsigned x = 0; x < 32; x++)
            for (unsigned b = 0; b < source_bpp; b++)
                assert(mapped[65536 + (y * 32 + x) * source_bpp + b] == pattern(x, y, b, generation));
            cases++;
        }
        vkDestroyImage(device, scratch, NULL); vkFreeMemory(device, scratch_memory, NULL);
        vkDestroyImage(device, source.image, NULL); vkFreeMemory(device, source_memory, NULL);
        vkDestroyImage(device, destination.image, NULL); vkFreeMemory(device, destination_memory, NULL);
    }
    printf("linear-byte GPU: %u format/scale/pitch/redraw cases preserve every active byte\n", cases);
    vkDestroyBuffer(device, renderer.storage_buffers[0].buffer, NULL); vkFreeMemory(device, transfer_memory, NULL);
    vkUnmapMemory(device, host_memory);
    vkDestroyBuffer(device, host, NULL); vkFreeMemory(device, host_memory, NULL);
    vkDestroyFence(device, fence, NULL); vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL);
}
