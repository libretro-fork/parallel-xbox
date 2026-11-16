/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <vk-test-loader.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/spv/clear.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
/* HELPERS */
typedef struct RenderPassState { VkFormat color_format, zeta_format; } RenderPassState;
typedef struct Binding { struct { VkImageAspectFlags aspect; } host_fmt; } Binding;
typedef struct PGRAPHVkState {
    VkDevice device;
    bool dyn_blend;
    VkShaderModule quad_vert_module, solid_frag_module;
    VkPipelineCache vk_pipeline_cache;
    VkPipelineLayout clear_pipeline_layout;
    VkCommandBuffer command_buffer;
    Binding *color_binding, *zeta_binding;
    struct { bool valid; } dyn_last;
} PGRAPHVkState;
typedef struct PGRAPHState { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
static VkRenderPass current_pass;
static VkRenderPass get_render_pass(PGRAPHVkState *r, const RenderPassState *s)
{ (void)r; (void)s; return current_pass; }
static float target;
static void pgraph_get_clear_color(PGRAPHState *pg, float *value)
{ (void)pg; for (unsigned i=0;i<4;i++) value[i]=target; }
static void pgraph_get_clear_depth_stencil_value(PGRAPHState *pg,float *depth,int *stencil)
{ (void)pg; *depth=target; *stencil=target?255:0; }
/* PRODUCTION */
#include "vk-clear-parent-reference.h"
static void emit(PGRAPHState *pg,uint32_t parameter,VkClearRect clear_rect)
{
    PGRAPHVkState *r=pg->vk_renderer_state;
    bool write_color=parameter&NV097_CLEAR_SURFACE_COLOR;
    bool write_zeta=parameter&(NV097_CLEAR_SURFACE_Z|NV097_CLEAR_SURFACE_STENCIL);
    /* COMMANDS */
}
typedef struct Image { VkImage image; VkDeviceMemory memory; VkImageView view; } Image;
static Image make_image(VkFormat format,VkImageAspectFlags aspect)
{
    Image image={0};
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=format,.extent={WIDTH,HEIGHT,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
            (aspect==VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)};
    CHECK(vkCreateImage(device,&ci,NULL,&image.image));
    VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image.image,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    CHECK(vkAllocateMemory(device,&ai,NULL,&image.memory));CHECK(vkBindImageMemory(device,image.image,image.memory,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image.image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={aspect,0,1,0,1}};
    CHECK(vkCreateImageView(device,&vi,NULL,&image.view));return image;
}
static void drop_image(Image image)
{ vkDestroyImageView(device,image.view,NULL);vkDestroyImage(device,image.image,NULL);vkFreeMemory(device,image.memory,NULL); }
static void copy_image(Image image,VkImageAspectFlags aspect,Buffer out)
{
    VkBufferImageCopy copy={.imageSubresource={aspect,0,0,1},.imageExtent={WIDTH,HEIGHT,1}};
    vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,out.buffer,1,&copy);
    host_barrier(out,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
}
static uint32_t color_value(unsigned format,unsigned mask)
{
    switch(format) {
    case 0:return ((mask&1)?0x7c00:0)|((mask&2)?0x03e0:0)|((mask&4)?0x001f:0)|((mask&8)?0x8000:0);
    case 1:return ((mask&1)?0xf800:0)|((mask&2)?0x07e0:0)|((mask&4)?0x001f:0);
    case 2:return ((mask&1)?0x00ff0000:0)|((mask&2)?0x0000ff00:0)|((mask&4)?0x000000ff:0)|((mask&8)?0xff000000:0);
    case 3:return (mask&1)?255:0;
    case 4:return ((mask&1)?255:0)|((mask&2)?0xff00:0);
    default:abort();
    }
}
int main(void)
{
    gpu_init();assert(vkCmdSetColorWriteMaskEXT);
    PGRAPHVkState r={.device=device,.command_buffer=command};PGRAPHState pg={&r};
    SpvStaticModule vs=pgraph_spv_fullscreen_vertex(),fs=pgraph_spv_solid_fragment();
    r.quad_vert_module=gpu_module(vs.words,vs.num_words);r.solid_frag_module=gpu_module(fs.words,fs.num_words);
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    CHECK(vkCreatePipelineLayout(device,&li,NULL,&r.clear_pipeline_layout));
    const VkFormat formats[]={VK_FORMAT_A1R5G5B5_UNORM_PACK16,VK_FORMAT_R5G6B5_UNORM_PACK16,
        VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8_UNORM,VK_FORMAT_R8G8_UNORM};
    const unsigned bytes[]={2,2,4,1,2};
    const VkFormat depths[]={VK_FORMAT_UNDEFINED,VK_FORMAT_D16_UNORM,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT};
    unsigned compared=0;
    for(unsigned f=0;f<5;f++)for(unsigned z=0;z<4;z++) {
        VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(physical,formats[f],&props);
        assert((props.optimalTilingFeatures&(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT))==
            (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT));
        if(z){vkGetPhysicalDeviceFormatProperties(physical,depths[z],&props);assert(props.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);}
        VkImageAspectFlags aspect=VK_IMAGE_ASPECT_DEPTH_BIT|(z>=2?VK_IMAGE_ASPECT_STENCIL_BIT:0);
        Image color=make_image(formats[f],VK_IMAGE_ASPECT_COLOR_BIT),depth={0};if(z)depth=make_image(depths[z],aspect);
        Binding cb={0},zb={.host_fmt.aspect=aspect};r.color_binding=&cb;r.zeta_binding=z?&zb:NULL;
        VkAttachmentDescription attachments[2]={
            {.format=formats[f],.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
             .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
            {.format=depths[z],.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
             .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE,
             .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}};
        VkAttachmentReference cr={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,
            .pColorAttachments=&cr,.pDepthStencilAttachment=z?&dr:NULL};
        VkSubpassDependency dep={.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,
            .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
        VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=z?2:1,.pAttachments=attachments,
            .subpassCount=1,.pSubpasses=&sub,.dependencyCount=1,.pDependencies=&dep};CHECK(vkCreateRenderPass(device,&ri,NULL,&current_pass));
        VkImageView views[]={color.view,depth.view};VkFramebuffer fb;
        VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=current_pass,
            .attachmentCount=z?2:1,.pAttachments=views,.width=WIDTH,.height=HEIGHT,.layers=1};CHECK(vkCreateFramebuffer(device,&fi,NULL,&fb));
        Buffer out=make_buffer(WIDTH*HEIGHT*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT),dout=make_buffer(WIDTH*HEIGHT*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            sout=make_buffer(WIDTH*HEIGHT,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        RenderPassState state={formats[f],depths[z]};
        for(unsigned mode=0;mode<3;mode++) {
            unsigned dynamic=mode==2;
            r.dyn_blend=dynamic;VkPipeline ready=VK_NULL_HANDLE;
            if(dynamic)CHECK(create_clear_native(&r,&state,NV097_CLEAR_SURFACE_R,&ready));
            for(unsigned mask=1;mask<15;mask++) {
                VkPipeline pipeline=ready;
                if(mode==1)CHECK(create_clear_native(&r,&state,mask<<4,&pipeline));
                for(unsigned flags=0;flags<4;flags++)for(unsigned region=0;region<3;region++)for(unsigned invert=0;invert<2;invert++) {
                    /* Stencil-only on D16 is invalid in the original command path; handled separately. */
                    if(z==1&&flags==2)continue;
                    uint32_t parameter=(mask<<4)|((flags&1)?NV097_CLEAR_SURFACE_Z:0)|((flags&2)?NV097_CLEAR_SURFACE_STENCIL:0);
                    if(mode==0)CHECK(parent_clear_native(&r,&state,parameter,&pipeline));
                    target=invert?0:1;
                    unsigned x0=region?3:0,y0=region?4:0,x1=region?WIDTH-4:WIDTH,y1=region?HEIGHT-3:HEIGHT;
                    if(region==2){x0=WIDTH-1;y0=HEIGHT-1;x1=WIDTH;y1=HEIGHT;}
                    VkClearRect rect={.rect={{x0,y0},{x1-x0,y1-y0}},.layerCount=1};
                    VkClearValue values[2]={0};for(unsigned c=0;c<4;c++)values[0].color.float32[c]=invert?1:0;
                    values[1].depthStencil.depth=invert?1:0;values[1].depthStencil.stencil=invert?255:0;
                    begin();VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=current_pass,
                        .framebuffer=fb,.renderArea={{0,0},{WIDTH,HEIGHT}},.clearValueCount=z?2:1,.pClearValues=values};
                    vkCmdBeginRenderPass(command,&bi,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
                    VkViewport viewport={0,0,WIDTH,HEIGHT,0,1};vkCmdSetViewport(command,0,1,&viewport);
                    emit(&pg,parameter,rect);
                    vkCmdEndRenderPass(command);copy_image(color,VK_IMAGE_ASPECT_COLOR_BIT,out);
                    if(z)copy_image(depth,VK_IMAGE_ASPECT_DEPTH_BIT,dout);if(z>=2)copy_image(depth,VK_IMAGE_ASPECT_STENCIL_BIT,sout);submit();
                    for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++) {
                        unsigned index=y*WIDTH+x;bool inside=x>=x0&&x<x1&&y>=y0&&y<y1;
                        unsigned changed=inside?mask:0;uint32_t wanted=color_value(f,invert?(15^changed):changed),actual=0;
                        memcpy(&actual,(char*)out.mapped+index*bytes[f],bytes[f]);assert(actual==wanted);
                        if(z){bool changed_depth=inside&&(flags&1);bool one=changed_depth?!invert:invert;
                            if(z==1)assert(((uint16_t*)dout.mapped)[index]==(one?65535:0));
                            else if(z==2)assert((((uint32_t*)dout.mapped)[index]&0xffffff)==(one?0xffffff:0));
                            else assert(((float*)dout.mapped)[index]==(one?1.0f:0.0f));}
                        if(z>=2){bool one=inside&&(flags&2)?!invert:invert;assert(((uint8_t*)sout.mapped)[index]==(one?255:0));}
                        compared++;
                    }
                    gpu_cases++;
                    if(mode==0)vkDestroyPipeline(device,pipeline,NULL);
                }
                if(mode==1)vkDestroyPipeline(device,pipeline,NULL);
            }
            if(dynamic)vkDestroyPipeline(device,ready,NULL);
        }
        drop_buffer(out);drop_buffer(dout);drop_buffer(sout);vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,current_pass,NULL);
        drop_image(color);if(z)drop_image(depth);
    }
    assert(gpu_cases==18900&&!errors);printf("Masked clear GPU: %u exact color/depth/stencil draws, %u pixels; parent/static/dynamic PASS; validation=%u\n",gpu_cases,compared,validation_enabled);
    vkDestroyPipelineLayout(device,r.clear_pipeline_layout,NULL);vkDestroyShaderModule(device,r.quad_vert_module,NULL);vkDestroyShaderModule(device,r.solid_frag_module,NULL);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}
    vkDestroyInstance(instance,NULL);test_vk_finalize();return 0;
}
