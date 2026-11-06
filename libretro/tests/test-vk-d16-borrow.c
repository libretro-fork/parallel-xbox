/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <volk.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
/* GPU HELPERS */
typedef struct SurfaceBinding {bool color,in_shader_read;VkImage image;struct {VkFormat vk_format;} host_fmt;unsigned borrow_submit;} SurfaceBinding;
typedef struct {bool cubemap;unsigned dimensionality;} TextureShape;
typedef struct {struct {TextureShape state;} key;VkImage image;VkImageView image_view;VkImageCreateInfo image_ci;bool s2t_borrowed;} TextureBinding;
typedef struct {VkDevice device;unsigned submit_count;TextureBinding *texture_bindings[4];} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;bool texture_dirty[4];} PGRAPHState;
typedef struct {VkFormat vk_format;VkComponentMapping component_map;} VkColorFormatInfo;
#define NV2A_MAX_TEXTURES 4
#define VK_CHECK CHECK
static unsigned begins;
static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg){(void)pg;begins++;return command;}
static void pgraph_vk_end_nondraw_commands(PGRAPHState *pg,VkCommandBuffer cmd){(void)pg;(void)cmd;}
static const VkImageViewType dimensionality_to_vk_image_view_type[]={0,VK_IMAGE_VIEW_TYPE_1D,VK_IMAGE_VIEW_TYPE_2D,VK_IMAGE_VIEW_TYPE_3D};
/* PRODUCTION */
typedef struct {VkImage image;VkImageView view;VkDeviceMemory memory;} Image;
static PGRAPHVkState renderer;static PGRAPHState pg;
static bool negative;
static bool capture=true;
static PFN_vkCmdPipelineBarrier real_barrier;
static unsigned depth_reads,depth_writes;
static VKAPI_ATTR void VKAPI_CALL observed_barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t nm,const VkMemoryBarrier *m,uint32_t nb,const VkBufferMemoryBarrier *b,uint32_t ni,const VkImageMemoryBarrier *images){
 for(unsigned i=0;i<ni;i++)if(images[i].subresourceRange.aspectMask==VK_IMAGE_ASPECT_DEPTH_BIT){
  const VkImageMemoryBarrier *v=&images[i];VkPipelineStageFlags tests=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  if(v->oldLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL&&v->newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){assert((src&tests)==tests&&dst==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT&&v->srcAccessMask==VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT&&v->dstAccessMask==VK_ACCESS_SHADER_READ_BIT);depth_reads++;}
  if(v->oldLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&v->newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL){assert(src==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT&&(dst&tests)==tests&&v->srcAccessMask==VK_ACCESS_SHADER_READ_BIT&&v->dstAccessMask==(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));depth_writes++;}
 }
 real_barrier(cmd,src,dst,flags,nm,m,nb,b,ni,images);
}
static VkRenderPass pass;static VkFramebuffer framebuffer;static Image target;
static VkPipeline sampling_pipeline;static VkPipelineLayout sampling_layout;
static Image image(VkFormat format,VkImageAspectFlags aspect){
 Image a={0};VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={256,256,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|(aspect==VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:0)|(format==VK_FORMAT_R32G32B32A32_UINT?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:0)};CHECK(vkCreateImage(device,&ci,NULL,&a.image));
 VkMemoryRequirements mr;vkGetImageMemoryRequirements(device,a.image,&mr);VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=memory_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};CHECK(vkAllocateMemory(device,&ai,NULL,&a.memory));CHECK(vkBindImageMemory(device,a.image,a.memory,0));
 TextureBinding node={.image=a.image,.image_ci=ci};node.key.state.dimensionality=2;
 VkColorFormatInfo vkf={.component_map={negative&&aspect==VK_IMAGE_ASPECT_DEPTH_BIT?VK_COMPONENT_SWIZZLE_ZERO:VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO}};
 if(format==VK_FORMAT_R32G32B32A32_UINT)vkf.component_map=(VkComponentMapping){0};
 create_texture_view(&renderer,&node,&vkf);a.view=node.image_view;return a;
}
static void transition(Image a,VkImageAspectFlags aspect,VkImageLayout old,VkImageLayout next){
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=next==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT,.oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=a.image,.subresourceRange={aspect,0,1,0,1}};
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
static void sample(VkDescriptorSet set,Buffer output,const float *push){
 VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={{0,0},{256,256}}};
 vkCmdBeginRenderPass(command,&bi,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,sampling_pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,sampling_layout,0,1,&set,0,NULL);vkCmdPushConstants(command,sampling_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,16,push);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
 if(!capture)return;
 VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={256,256,1}};vkCmdCopyImageToBuffer(command,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.buffer,1,&copy);host_barrier(output,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
}
static unsigned mismatches(Buffer output){uint32_t *v=output.mapped;unsigned n=0;for(unsigned i=0;i<65536;i++)n+=v[i*4+2]!=0;return n;}
static void snapshot_reference(SurfaceBinding *surface,Image reference,Buffer buffer){
 PGRAPHVkState *r=&renderer;PGRAPHState *pg0=&pg;PGRAPHState *pg=pg0;VkCommandBuffer cmd=command;
 /* COPY SOURCE BEGIN */
 VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={256,256,1}};vkCmdCopyImageToBuffer(command,surface->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer.buffer,1,&copy);
 /* COPY SOURCE END */
 pgraph_vk_transition_image_layout(pg,command,reference.image,VK_FORMAT_R16_UNORM,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 VkBufferMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer.buffer,.size=VK_WHOLE_SIZE};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&b,0,NULL);
 copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;vkCmdCopyBufferToImage(command,buffer.buffer,reference.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
 pgraph_vk_transition_image_layout(pg,command,reference.image,VK_FORMAT_R16_UNORM,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&b,0,NULL);
}
int main(int argc,char **argv){
 bool benchmark=argc>1&&!strcmp(argv[1],"benchmark");negative=argc>1&&!benchmark;gpu_init();renderer.device=device;renderer.submit_count=7;pg.vk_renderer_state=&renderer;
 real_barrier=vkCmdPipelineBarrier;vkCmdPipelineBarrier=observed_barrier;
 VkFormatProperties dp;vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_D16_UNORM,&dp);printf("D16 sampling features: %08x\n",dp.optimalTilingFeatures);
 Image imgs[]={image(VK_FORMAT_D16_UNORM,VK_IMAGE_ASPECT_DEPTH_BIT),image(VK_FORMAT_R16_UNORM,VK_IMAGE_ASPECT_COLOR_BIT)};
 Buffer upload=make_buffer(65536*2,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),output=make_buffer(65536*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 for(unsigned i=0;i<65536;i++)((uint16_t*)upload.mapped)[i]=(uint16_t)(i*40503u+313u);
 begin();for(unsigned i=0;i<2;i++){VkImageAspectFlags aspect=i?VK_IMAGE_ASPECT_COLOR_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;transition(imgs[i],aspect,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkBufferImageCopy c={.imageSubresource={aspect,0,0,1},.imageExtent={256,256,1}};vkCmdCopyBufferToImage(command,upload.buffer,imgs[i].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);pgraph_vk_transition_image_layout(&pg,command,imgs[i].image,i?VK_FORMAT_R16_UNORM:VK_FORMAT_D16_UNORM,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,i?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);}
 SurfaceBinding surface={.image=imgs[0].image,.host_fmt={VK_FORMAT_D16_UNORM}};
 TextureBinding alias={.image=imgs[0].image,.s2t_borrowed=true},other={.image=imgs[1].image};renderer.texture_bindings[0]=renderer.texture_bindings[2]=&alias;renderer.texture_bindings[1]=&other;
 s2t_borrow_sync(&pg,&surface);assert(surface.in_shader_read&&surface.borrow_submit==7&&begins==1);s2t_borrow_sync(&pg,&surface);assert(begins==1);submit();
 VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL}};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout dsl;CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&dsl));
 VkPushConstantRange range={VK_SHADER_STAGE_FRAGMENT_BIT,0,16};VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&dsl,.pushConstantRangeCount=1,.pPushConstantRanges=&range};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&pi,NULL,&layout));
 VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6}};VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=3,.poolSizeCount=1,.pPoolSizes=sizes};VkDescriptorPool pool0;CHECK(vkCreateDescriptorPool(device,&pci,NULL,&pool0));VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool0,.descriptorSetCount=1,.pSetLayouts=&dsl};VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&dai,&set));
 const char *source="#version 450\nlayout(set=0,binding=0) uniform sampler2D d;layout(set=0,binding=1) uniform sampler2D r;layout(location=0) out uvec4 result;layout(push_constant) uniform P {vec4 p;};void main(){uint i=uint(gl_FragCoord.x)+uint(gl_FragCoord.y)*256u;vec2 phase=p.xy;if(p.x < -16.0)phase=vec2(i%256u,i/256u)/256.0;vec2 uv=(vec2(i%256u,i/256u)+phase)/256.0+p.zw;uvec4 a=floatBitsToUint(textureLod(d,uv,0.0)),b=floatBitsToUint(textureLod(r,uv,0.0));uint mask=uint(a.x!=b.x)+2u*uint(a.y!=b.y)+4u*uint(a.z!=b.z)+8u*uint(a.w!=b.w);result=uvec4(a.x,b.x,mask,0u);}";
 GlslcOptions opts={0};GlslcResult result={0};if(!glslc_compile(GLSLC_STAGE_FRAGMENT,source,&opts,&result)){fprintf(stderr,"%s\n",result.error);return 1;}VkShaderModule module=gpu_module(result.words,result.num_words);free(result.words);
 const char *vertex="#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex&1)*4-1,(gl_VertexIndex&2)*2-1);gl_Position=vec4(p,0,1);}";
 if(!glslc_compile(GLSLC_STAGE_VERTEX,vertex,&opts,&result)){fprintf(stderr,"%s\n",result.error);return 1;}VkShaderModule vs=gpu_module(result.words,result.num_words);free(result.words);
 target=image(VK_FORMAT_R32G32B32A32_UINT,VK_IMAGE_ASPECT_COLOR_BIT);
 begin();SurfaceBinding color_control={.color=true,.image=target.image,.host_fmt={VK_FORMAT_R32G32B32A32_UINT}};
 pgraph_vk_transition_image_layout(&pg,command,target.image,VK_FORMAT_R32G32B32A32_UINT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);s2t_borrow_sync(&pg,&color_control);assert(color_control.in_shader_read);pgraph_vk_surface_ensure_attachment_layout(&pg,&color_control);assert(!color_control.in_shader_read);submit();
 VkAttachmentDescription attachment={.format=VK_FORMAT_R32G32B32A32_UINT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};VkSubpassDependency dep[]={
 {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
 {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT}};
 VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=2,.pDependencies=dep};CHECK(vkCreateRenderPass(device,&rp,NULL,&pass));VkFramebufferCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&target.view,.width=256,.height=256,.layers=1};CHECK(vkCreateFramebuffer(device,&fc,NULL,&framebuffer));
 sampling_layout=layout;sampling_pipeline=pipeline(vs,module,VK_NULL_HANDLE,layout,pass,(VkViewport){0,0,256,256,0,1},(VkRect2D){{0,0},{256,256}});
 const VkSamplerAddressMode addresses[]={VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER};
 const float phases[][2]={{0.5f,0.5f},{0,0},{0.25f,0.75f},{0.75f,0.25f},{0.125f,0.875f},{-0.5f,1.5f},{0.375f,0.625f},{1,1},{-32,-32}};
 unsigned failures=0,cases=0;for(unsigned filter=0;filter<2&&!benchmark;filter++){
 if(filter&&!(dp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)){puts("SKIP: D16 linear unsupported");continue;}
 for(unsigned address=0;address<4;address++)for(unsigned border=0;border<3;border++){
  VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=filter?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=filter?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=addresses[address],.addressModeV=addresses[address],.addressModeW=addresses[address],.borderColor=(VkBorderColor)(border*2),.maxLod=0};VkSampler sampler;CHECK(vkCreateSampler(device,&si,NULL,&sampler));
  VkDescriptorImageInfo ii[]={{sampler,imgs[0].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,imgs[1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};VkWriteDescriptorSet writes[]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii[0]},{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii[1]}};vkUpdateDescriptorSets(device,2,writes,0,NULL);
  for(unsigned phase=0;phase<9;phase++)for(unsigned shift=0;shift<3;shift++){
   float push[]={phases[phase][0],phases[phase][1],shift==0?0:shift==1?-1.003f:1.003f,shift==0?0:shift==1?-1.003f:1.003f};
   begin();sample(set,output,push);submit();
   uint32_t *values=output.mapped;unsigned bad=0,first=0,mask=0;for(unsigned i=0;i<65536;i++)if(values[i*4+2]){if(!bad)first=i;bad++;mask|=values[i*4+2];}
   cases++;if(bad){if(failures<12)printf("DIFF filter=%u address=%u border=%u phase=%u shift=%u bad=%u mask=%u first=%u red=%08x/%08x\n",filter,address,border,phase,shift,bad,mask,first,values[first*4],values[first*4+1]);failures++;}
  }
  vkDestroySampler(device,sampler,NULL);
 }
 }
 /* A private copy on another unit must restore the shared sampled image. */
 VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};VkSampler sampler;CHECK(vkCreateSampler(device,&si,NULL,&sampler));
 VkDescriptorImageInfo ii[]={{sampler,imgs[0].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,imgs[1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};VkWriteDescriptorSet writes[2];memset(writes,0,sizeof(writes));for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii[i]};vkUpdateDescriptorSets(device,2,writes,0,NULL);
 Buffer transfer=make_buffer(65536*2,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);float center[]={0.5f,0.5f,0,0};
 begin();renderer.submit_count++;snapshot_reference(&surface,imgs[1],transfer);assert(surface.in_shader_read&&surface.borrow_submit==renderer.submit_count);sample(set,output,center);submit();assert(!mismatches(output)||negative);
 /* Retarget and rewrite on the GPU; existing borrowed units become dirty. */
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=imgs[0].image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_D16_UNORM,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};VkImageView depth_view;CHECK(vkCreateImageView(device,&vi,NULL,&depth_view));
 VkAttachmentDescription da={.format=VK_FORMAT_D16_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkAttachmentReference dr={0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription ds={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.pDepthStencilAttachment=&dr};VkSubpassDependency dd={.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT};VkRenderPassCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&da,.subpassCount=1,.pSubpasses=&ds,.dependencyCount=1,.pDependencies=&dd};VkRenderPass depth_pass;CHECK(vkCreateRenderPass(device,&dpi,NULL,&depth_pass));fc.renderPass=depth_pass;fc.pAttachments=&depth_view;VkFramebuffer depth_fb;CHECK(vkCreateFramebuffer(device,&fc,NULL,&depth_fb));
 for(unsigned generation=0;generation<3;generation++){
  begin();renderer.submit_count++;memset(pg.texture_dirty,0,sizeof(pg.texture_dirty));pgraph_vk_surface_ensure_attachment_layout(&pg,&surface);assert(!surface.in_shader_read&&pg.texture_dirty[0]&&pg.texture_dirty[2]&&!pg.texture_dirty[1]);
  VkClearValue clear={.depthStencil={0.13f+generation*0.31f,0}};VkRenderPassBeginInfo bi0={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=depth_pass,.framebuffer=depth_fb,.renderArea={{0,0},{256,256}},.clearValueCount=1,.pClearValues=&clear};vkCmdBeginRenderPass(command,&bi0,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
  snapshot_reference(&surface,imgs[1],transfer);assert(!surface.in_shader_read);s2t_borrow_sync(&pg,&surface);assert(surface.in_shader_read&&surface.borrow_submit==renderer.submit_count);sample(set,output,center);submit();assert(!mismatches(output)||negative);
 }
 if(benchmark){
  const char *single="#version 450\nlayout(set=0,binding=0) uniform sampler2D d;layout(location=0) out uvec4 result;layout(push_constant) uniform P {vec4 p;};void main(){result=floatBitsToUint(textureLod(d,gl_FragCoord.xy/256.0,0.0));}";
  assert(glslc_compile(GLSLC_STAGE_FRAGMENT,single,&opts,&result));VkShaderModule bm=gpu_module(result.words,result.num_words);free(result.words);VkPipeline saved=sampling_pipeline;sampling_pipeline=pipeline(vs,bm,VK_NULL_HANDLE,layout,pass,(VkViewport){0,0,256,256,0,1},(VkRect2D){{0,0},{256,256}});
  const char *writer="#version 450\nlayout(push_constant) uniform P {vec4 p;};void main(){gl_FragDepth=fract((floor(gl_FragCoord.x)*17.0+floor(gl_FragCoord.y)*31.0+p.x*13.0)/4096.0);}";
  assert(glslc_compile(GLSLC_STAGE_FRAGMENT,writer,&opts,&result));VkShaderModule wm=gpu_module(result.words,result.num_words);free(result.words);VkPipeline wp=depth_pipeline(vs,wm,VK_NULL_HANDLE,layout,depth_pass,(VkViewport){0,0,256,256,0,1},(VkRect2D){{0,0},{256,256}});
  VkDescriptorSet sets[2];for(unsigned i=0;i<2;i++){CHECK(vkAllocateDescriptorSets(device,&dai,&sets[i]));VkDescriptorImageInfo info={sampler,imgs[i?0:1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[i],.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&write,0,NULL);}
  VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);VkQueryPoolCreateInfo qi={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};VkQueryPool query;CHECK(vkCreateQueryPool(device,&qi,NULL,&query));capture=false;
  for(unsigned trial=0;trial<16;trial++)for(unsigned order=0;order<2;order++){
   unsigned borrow=(trial+order)%2;begin();renderer.submit_count++;vkCmdResetQueryPool(command,query,0,2);vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,query,0);
   for(unsigned n=0;n<8;n++){
    pgraph_vk_surface_ensure_attachment_layout(&pg,&surface);VkClearValue clear={.depthStencil={0.3f+(n%4)*0.1f,0}};VkRenderPassBeginInfo b={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=depth_pass,.framebuffer=depth_fb,.renderArea={{0,0},{256,256}},.clearValueCount=1,.pClearValues=&clear};vkCmdBeginRenderPass(command,&b,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,wp);float push[]={n+trial,0,0,0};vkCmdPushConstants(command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,16,push);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
    if(borrow)s2t_borrow_sync(&pg,&surface);else snapshot_reference(&surface,imgs[1],transfer);sample(sets[borrow],output,center);
   }
   vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query,1);submit();uint64_t times[2];CHECK(vkGetQueryPoolResults(device,query,0,2,sizeof(times),times,8,VK_QUERY_RESULT_64_BIT));printf("TIME,%u,%u,%.3f\n",trial,borrow,(double)(times[1]-times[0])*props.limits.timestampPeriod/8);
  }
  vkDestroyQueryPool(device,query,NULL);vkDestroyPipeline(device,wp,NULL);vkDestroyShaderModule(device,wm,NULL);vkDestroyPipeline(device,sampling_pipeline,NULL);vkDestroyShaderModule(device,bm,NULL);sampling_pipeline=saved;
 }
 vkDestroyFramebuffer(device,depth_fb,NULL);vkDestroyRenderPass(device,depth_pass,NULL);vkDestroyImageView(device,depth_view,NULL);drop_buffer(transfer);vkDestroySampler(device,sampler,NULL);
 vkDestroyPipeline(device,sampling_pipeline,NULL);vkDestroyShaderModule(device,vs,NULL);vkDestroyShaderModule(device,module,NULL);vkDestroyDescriptorPool(device,pool0,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,dsl,NULL);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);
 for(unsigned i=0;i<3;i++){Image a=i<2?imgs[i]:target;vkDestroyImageView(device,a.view,NULL);vkDestroyImage(device,a.image,NULL);vkFreeMemory(device,a.memory,NULL);}drop_buffer(upload);drop_buffer(output);
 vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}vkDestroyInstance(instance,NULL);
 printf("RESULT: %u batches x 65536 values, %u mismatching batches, validation_errors=%u; private-copy restore and three GPU writer generations passed\n",cases,failures,errors);assert(!errors&&depth_reads>=4&&depth_writes>=3);return failures?1:0;
}
