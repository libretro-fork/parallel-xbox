/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ctz32(v) __builtin_ctz(v)
#define NV2A_VK_DPRINTF(...) ((void)0)
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define nv2a_profile_inc_counter(...) ((void)0)
#define pgraph_vk_begin_debug_marker(...) ((void)0)
#define pgraph_vk_end_debug_marker(...) ((void)0)
typedef struct { VkFormat color_format,zeta_format; } RenderPassState;
typedef struct {
    unsigned width,height;
    struct { VkImageAspectFlags aspect; } host_fmt;
    VkImageView image_view;
} SurfaceBinding;
struct XemuVkFramebufferEntry {
    VkImageView color,zeta;VkRenderPass render_pass;unsigned width,height,last_use;
    VkFramebuffer framebuffer;
};
typedef struct {
    VkDevice device;VkRenderPass render_pass;VkFramebuffer framebuffer;VkCommandBuffer command_buffer;
    SurfaceBinding *color_binding,*zeta_binding;
    bool in_command_buffer,in_render_pass;unsigned clear_parameter,framebuffer_use_counter;
    int framebuffer_cache_n;struct XemuVkFramebufferEntry framebuffer_cache[4];
    struct { bool valid; } dyn_last;
} PGRAPHVkState;
typedef struct {
    PGRAPHVkState *vk_renderer_state;unsigned scale;bool clearing;
    struct { unsigned width,height; } surface_binding_dim;
    uint32_t rect_x,rect_y;
} PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
static int pgraph_vk_cost_ops;
static bool xemu_cost_blit_suspend(NV2AState *d,int *ops) { return false; }
static void xemu_cost_blit_resume(NV2AState *d,int *ops) { assert(false); }
static void retire_framebuffer_entry(PGRAPHVkState *r,int i) { assert(false); }
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *w,unsigned *h) { *w*=pg->scale;*h*=pg->scale; }
static void pgraph_apply_anti_aliasing_factor(PGRAPHState *pg,unsigned *w,unsigned *h) {}
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg) {
    assert(reg==NV_PGRAPH_CLEARRECTX || reg==NV_PGRAPH_CLEARRECTY);
    return reg==NV_PGRAPH_CLEARRECTX?pg->rect_x:pg->rect_y;
}
static void pgraph_vk_surface_update(NV2AState *d,bool upload,bool color,bool zeta) {
    PGRAPHVkState *r=d->pgraph.vk_renderer_state;assert(upload && r->zeta_binding && r->color_binding);
}
static void pgraph_get_clear_color(PGRAPHState *pg,float *c) { c[0]=1;c[1]=c[2]=0;c[3]=1; }
static void pgraph_get_clear_depth_stencil_value(PGRAPHState *pg,float *depth,int *stencil) { *depth=0.25f;*stencil=0x3c; }
static void pgraph_vk_set_surface_dirty(PGRAPHState *pg,bool color,bool zeta) { assert(pg->clearing && zeta); }
static void create_frame_buffer(PGRAPHState *pg);
static void begin_render_pass(PGRAPHState *pg);
static void begin_pre_draw(PGRAPHState *pg) { create_frame_buffer(pg); }
static void begin_draw(PGRAPHState *pg) { begin_render_pass(pg); }
static void end_draw(PGRAPHState *pg) { vkCmdEndRenderPass(pg->vk_renderer_state->command_buffer);pg->vk_renderer_state->in_render_pass=false; }
/* PRODUCTION */
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
static unsigned memory_type(unsigned bits,VkMemoryPropertyFlags flags) {
    for(unsigned i=0;i<memory_properties.memoryTypeCount;i++)
        if((bits&(1u<<i)) && (memory_properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
    abort();
}
typedef struct { VkImage handle;VkImageView view;VkDeviceMemory memory; } Image;
static Image image(unsigned w,unsigned h,VkFormat format,VkImageAspectFlags aspect) {
    Image out={0};VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
            (aspect==VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)};
    VK_CHECK(vkCreateImage(device,&ci,NULL,&out.handle));
    VkMemoryRequirements req;vkGetImageMemoryRequirements(device,out.handle,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=memory_type(req.memoryTypeBits,0)};
    VK_CHECK(vkAllocateMemory(device,&ai,NULL,&out.memory));VK_CHECK(vkBindImageMemory(device,out.handle,out.memory,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=out.handle,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={aspect,0,1,0,1}};
    VK_CHECK(vkCreateImageView(device,&vi,NULL,&out.view));return out;
}
static void barrier(VkCommandBuffer cmd,Image img,VkImageAspectFlags aspect,VkImageLayout old,VkImageLayout next,
                    VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags read,VkAccessFlags write) {
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=read,.dstAccessMask=write,
        .oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=img.handle,.subresourceRange={aspect,0,1,0,1}};
    vkCmdPipelineBarrier(cmd,src,dst,0,0,NULL,0,NULL,1,&b);
}
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
    VkInstance instance;VK_CHECK(vkCreateInstance(&ici,NULL,&instance));
    PFN_vkCreateDebugUtilsMessengerEXT create=(void *)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void *)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};
    VkDebugUtilsMessengerEXT messenger;VK_CHECK(create(instance,&mi,NULL,&messenger));
    uint32_t n=1;VkPhysicalDevice gpu;VK_CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));
    vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);
    uint32_t nq=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families);
    unsigned family=0;while(family<nq && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family<nq);float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    VK_CHECK(vkCreateDevice(gpu,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VkCommandPool pool;VK_CHECK(vkCreateCommandPool(device,&pi,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;VK_CHECK(vkAllocateCommandBuffers(device,&ca,&cmd));
    const VkImageAspectFlags ds=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    const unsigned scales[]={1,2,4};
    for(unsigned si=0;si<3;si++) {
        unsigned scale=scales[si],w=32*scale,h=24*scale;size_t pixels=w*h;
        Image depth=image(w,h,VK_FORMAT_D32_SFLOAT_S8_UINT,ds);
        Image colors[]={image(8*scale,6*scale,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT),
                        image(w,h,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT)};
        SurfaceBinding c[2]={{.width=8,.height=6,.image_view=colors[0].view},
                             {.width=32,.height=24,.image_view=colors[1].view}};
        SurfaceBinding z={.width=32,.height=24,.image_view=depth.view,.host_fmt={ds}};
        PGRAPHVkState r={.device=device,.command_buffer=cmd,.color_binding=&c[0],.zeta_binding=&z,.in_command_buffer=true};
        NV2AState d={.pgraph={.vk_renderer_state=&r,.scale=scale,.surface_binding_dim={8,6}}};
        RenderPassState state={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_D32_SFLOAT_S8_UINT};
        r.render_pass=create_render_pass(&r,&state);
        VkBuffer buffer;VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=pixels*5,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VK_CHECK(vkCreateBuffer(device,&bi,NULL,&buffer));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);
        VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
            .memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        VkDeviceMemory mem;VK_CHECK(vkAllocateMemory(device,&ai,NULL,&mem));VK_CHECK(vkBindBufferMemory(device,buffer,mem,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_CHECK(vkBeginCommandBuffer(cmd,&begin));
        barrier(cmd,depth,ds,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkClearDepthStencilValue initial={0.75f,0xa5};VkImageSubresourceRange dr={ds,0,1,0,1};
        vkCmdClearDepthStencilImage(cmd,depth.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&initial,1,&dr);
        barrier(cmd,depth,ds,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        for(unsigned i=0;i<2;i++) {
            barrier(cmd,colors[i],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
            VkClearColorValue black={{0,0,0,1}};VkImageSubresourceRange cr={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdClearColorImage(cmd,colors[i].handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&cr);
            barrier(cmd,colors[i],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
        d.pgraph.rect_x=1|(5<<16);d.pgraph.rect_y=2|(4<<16);
        pgraph_vk_clear_surface(&d,NV097_CLEAR_SURFACE_COLOR|NV097_CLEAR_SURFACE_Z|NV097_CLEAR_SURFACE_STENCIL);
        r.color_binding=&c[1];d.pgraph.surface_binding_dim.width=32;d.pgraph.surface_binding_dim.height=24;
        d.pgraph.rect_x=20|(22<<16);d.pgraph.rect_y=10|(12<<16);
        pgraph_vk_clear_surface(&d,NV097_CLEAR_SURFACE_Z|NV097_CLEAR_SURFACE_STENCIL);
        r.color_binding=&c[0];d.pgraph.surface_binding_dim.width=8;d.pgraph.surface_binding_dim.height=6;
        d.pgraph.rect_x=0|(2<<16);d.pgraph.rect_y=0|(1<<16);
        pgraph_vk_clear_surface(&d,NV097_CLEAR_SURFACE_STENCIL);assert(r.framebuffer_cache_n==2);
        barrier(cmd,depth,ds,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copies[2]={{.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={w,h,1}},
            {.bufferOffset=pixels*4,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={w,h,1}}};
        vkCmdCopyImageToBuffer(cmd,depth.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,2,copies);
        VkBufferMemoryBarrier bb={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .buffer=buffer,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&bb,0,NULL);
        VK_CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
        VkFence fence;VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK_CHECK(vkCreateFence(device,&fi,NULL,&fence));
        VK_CHECK(vkQueueSubmit(queue,1,&submit,fence));VK_CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
        uint8_t *mapped;VK_CHECK(vkMapMemory(device,mem,0,VK_WHOLE_SIZE,0,(void **)&mapped));
        for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
            unsigned gx=x/scale,gy=y/scale;
            bool changed=(gx>=1 && gx<=5 && gy>=2 && gy<=4)||(gx>=20 && gx<=22 && gy>=10 && gy<=12);
            bool stencil=changed || (gx<=2 && gy<=1);float value;memcpy(&value,mapped+(y*w+x)*4,4);
            assert(value==(changed?0.25f:0.75f));assert(mapped[pixels*4+y*w+x]==(stencil?0x3c:0xa5));
        }
        vkUnmapMemory(device,mem);vkDestroyFence(device,fence,NULL);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,mem,NULL);
        for(int i=0;i<r.framebuffer_cache_n;i++) vkDestroyFramebuffer(device,r.framebuffer_cache[i].framebuffer,NULL);
        vkDestroyRenderPass(device,r.render_pass,NULL);
        Image all[]={depth,colors[0],colors[1]};for(unsigned i=0;i<3;i++) {
            vkDestroyImageView(device,all[i].view,NULL);vkDestroyImage(device,all[i].handle,NULL);vkFreeMemory(device,all[i].memory,NULL);
        }
        VK_CHECK(vkResetCommandBuffer(cmd,0));
    }
    vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
    printf("larger-depth clear: 1x/2x/4x, small/full/small framebuffer reuse and preserved depth/stencil passed; validation_errors=%u\n",errors);
    return errors?1:0;
}
