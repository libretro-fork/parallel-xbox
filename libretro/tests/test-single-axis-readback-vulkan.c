/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Execute the production readback with real images, RAM buffer and host wait. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "volk.h"
#define CHECK(call) do { VkResult check_result = (call); if (check_result != VK_SUCCESS) { fprintf(stderr,"%s: %d\n",#call,check_result); exit(1); } } while(0)
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
enum { BUFFER_COMPUTE_DST, BUFFER_STAGING_DST, BUFFER_UNSWIZZLE_OUT };
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define memory_region_size(p) (*(p))
#define NV2A_PROF_SURF_DOWNLOAD 0
#define NV2A_PROF_QUEUE_SUBMIT_1 1
#define NV2A_PROF_SURF_SWIZZLE 2
#define RGBA_RED 0
#define VK_FINISH_REASON_SURFACE_DOWN 0
#define PGRAPH_VK_RING_UNSWIZZLE 0
#define nv2a_profile_inc_counter(...) ((void)0)
#define trace_nv2a_pgraph_surface_download(...) ((void)0)
#define pgraph_vk_begin_debug_marker(...) ((void)0)
#define pgraph_vk_end_debug_marker(...) ((void)0)
typedef struct {VkBuffer buffer; VkDeviceSize buffer_size; void *mapped;} StorageBuffer;
typedef struct {
    StorageBuffer storage_buffers[3]; VkBuffer vram_buffer;
    VkPhysicalDeviceProperties device_props;
    struct {bool open;} readback_batch;
    bool in_command_buffer; int command_buffer_start_time;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state; unsigned surface_scale_factor;} PGRAPHState;
typedef struct {PGRAPHState pgraph; uint8_t *vram_ptr; size_t *vram;} NV2AState;
typedef struct {
    unsigned width,height,pitch; bool color,swizzle,in_shader_read;
    uint64_t vram_addr; int draw_time;
    struct {unsigned bytes_per_pixel;} fmt;
    struct {VkFormat vk_format; unsigned host_bytes_per_pixel; VkImageAspectFlags aspect;} host_fmt;
    VkImage image,image_scratch; VkImageLayout image_scratch_current_layout;
} SurfaceBinding;
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkCommandBuffer command;
static VkQueue queue;
static VkFence fence;
static VkDeviceMemory scratch_memory;
static unsigned claims, waits, batch_adds;
static VkDeviceSize claimed_bytes;

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

static void unexpected(void) { assert(!"unexpected staging/swizzle/packed fallback"); abort(); }
#define readback_batch_flush(...) ((void)0)
#define pgraph_vk_surface_borrow_pending(...) false
#define pgraph_vk_finish(...) unexpected()
#define pgraph_vk_buffer_ensure_capacity(...) unexpected()
#define pgraph_vk_swizzle_possible(...) false
#define pgraph_vk_swizzle_packed_possible(...) false
#define pgraph_vk_ring_alloc(...) (unexpected(), false)
#define pgraph_vk_swizzle_to_ram(...) unexpected()
#define pack_depth_readback(...) (unexpected(), VK_NULL_HANDLE)
#define packed_readback_to_host_barrier(...) unexpected()
#define invalidate_readback(...) unexpected()
#define swizzle_rect(...) unexpected()
#define memcpy_image(...) unexpected()
static void pgraph_apply_scaling_factor(PGRAPHState *p,unsigned *w,unsigned *h)
{*w*=p->surface_scale_factor;*h*=p->surface_scale_factor;}
static VkImageLayout pgraph_vk_surface_base_layout(const SurfaceBinding *s)
{return s->color?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;}
static void pgraph_vk_surface_left_shader_read(PGRAPHState *p,SurfaceBinding *s)
{(void)p;s->in_shader_read=false;}
static void pgraph_vk_transition_image_layout(PGRAPHState *p,VkCommandBuffer c,VkImage im,
    VkFormat f,VkImageLayout old,VkImageLayout next)
{
    (void)p;
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=im,
        .subresourceRange={f==VK_FORMAT_D16_UNORM?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
static void ensure_surface_scratch(PGRAPHVkState *r,SurfaceBinding *s)
{
    (void)r;if(s->image_scratch)return;
    s->image_scratch=image(s->host_fmt.vk_format,s->width,s->height,0,&scratch_memory);
    s->image_scratch_current_layout=VK_IMAGE_LAYOUT_UNDEFINED;
}
static VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *p)
{
    (void)p;CHECK(vkResetCommandBuffer(command,0));
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command,&bi));return command;
}
static VkCommandBuffer readback_batch_cmd(PGRAPHState *p)
{assert(p->vk_renderer_state->readback_batch.open);return pgraph_vk_begin_single_time_commands(p);}
static void readback_batch_add(NV2AState *d,SurfaceBinding *s)
{assert(d->pgraph.vk_renderer_state->readback_batch.open&&s->width);batch_adds++;}
static void submit_and_wait(void)
{
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    CHECK(vkQueueSubmit(queue,1,&si,fence));
    CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
    CHECK(vkResetFences(device,1,&fence));
}
static void pgraph_vk_end_single_time_commands(PGRAPHState *p,VkCommandBuffer c)
{(void)p;assert(c==command);submit_and_wait();waits++;}
static void lr_gpu_ram_claim(uintptr_t c,uint64_t addr,VkDeviceSize bytes)
{assert(c==(uintptr_t)command&&addr==65536);claims++;claimed_bytes=bytes;}
/* PRODUCTION */

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
    printf("single-axis readback GPU: %s\n", properties.deviceName);
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
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    CHECK(vkAllocateCommandBuffers(device, &ai, &command));
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    CHECK(vkCreateFence(device, &fi, NULL, &fence));

    VkDeviceMemory host_memory;
    VkBuffer host=buffer(131072,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&host_memory);
    unsigned char *mapped;
    CHECK(vkMapMemory(device,host_memory,0,VK_WHOLE_SIZE,0,(void **)&mapped));
    PGRAPHVkState r={.vram_buffer=host,.device_props=properties};
    r.storage_buffers[BUFFER_STAGING_DST].buffer_size=65536;
    size_t vram_size=131072;
    NV2AState d={.pgraph={&r,1},.vram_ptr=mapped,.vram=&vram_size};
    const VkFormat formats[]={VK_FORMAT_R8_UNORM,VK_FORMAT_R5G6B5_UNORM_PACK16,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_D16_UNORM};
    const unsigned lengths[]={1,4,64};unsigned cases=0;
    for(unsigned f=0;f<4;f++)for(unsigned axis=0;axis<2;axis++)
    for(unsigned n=0;n<3;n++)for(unsigned scale=1;scale<=4;scale*=2)
    for(unsigned batch=0;batch<2;batch++){
        unsigned bpp=f==0?1:f==2?4:2;
        unsigned w=axis?1:lengths[n],h=axis?lengths[n]:1;
        bool color=f!=3;VkImageAspectFlags aspect=color?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        VkDeviceMemory source_memory;
        SurfaceBinding s={.width=w,.height=h,.pitch=w*bpp,.color=color,.swizzle=true,.vram_addr=65536,
            .fmt={bpp},.host_fmt={formats[f],bpp,aspect},
            .image=image(formats[f],w*scale,h*scale,color?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,&source_memory)};
        d.pgraph.surface_scale_factor=scale;r.readback_batch.open=batch;
        for(unsigned gen=0;gen<2;gen++){
            for(unsigned y=0;y<h*scale;y++)for(unsigned x=0;x<w*scale;x++)for(unsigned b=0;b<bpp;b++)
                mapped[(y*w*scale+x)*bpp+b]=(x/scale)*37+(y/scale)*71+b*53+gen*109;
            memset(mapped+65520,0xcd,(size_t)w*h*bpp+32);
            pgraph_vk_begin_single_time_commands(&d.pgraph);
            pgraph_vk_transition_image_layout(&d.pgraph,command,s.image,formats[f],gen?pgraph_vk_surface_base_layout(&s):VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region={.imageSubresource={aspect,0,0,1},.imageExtent={w*scale,h*scale,1}};
            vkCmdCopyBufferToImage(command,host,s.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            pgraph_vk_transition_image_layout(&d.pgraph,command,s.image,formats[f],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,pgraph_vk_surface_base_layout(&s));
            submit_and_wait();claims=waits=batch_adds=0;
            bool done=download_surface_to_buffer(&d,&s,mapped+65536);
            assert(done==!batch&&waits==!batch&&batch_adds==batch);
            if(batch){readback_to_host_barrier(command);submit_and_wait();waits++;}
            assert(claims==1&&waits==1&&claimed_bytes==(VkDeviceSize)w*h*bpp);
            for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned b=0;b<bpp;b++)
                assert(mapped[65536+(y*w+x)*bpp+b]==(unsigned char)(x*37+y*71+b*53+gen*109));
            for(unsigned guard=0;guard<16;guard++)assert(mapped[65520+guard]==0xcd&&mapped[65536+w*h*bpp+guard]==0xcd);
            cases++;
        }
        if(s.image_scratch){vkDestroyImage(device,s.image_scratch,NULL);vkFreeMemory(device,scratch_memory,NULL);}
        vkDestroyImage(device,s.image,NULL);vkFreeMemory(device,source_memory,NULL);
    }
    printf("single-axis readback GPU: %u color/D16 axis/extent/scale/batch/update cases; no ring or swizzle, exact RAM bytes and guards, required wait retained\n",cases);
    vkUnmapMemory(device,host_memory);vkDestroyBuffer(device,host,NULL);vkFreeMemory(device,host_memory,NULL);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
}
