/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Source/arithmetic qualification only: native modules are recreated per case.
 * 3072 original positives, 3072 retained unsupported-input negative admissions,
 * and 3072 supplementary supported eight-stage/final-sum positives. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"
#include "hw/xbox/nv2a/pgraph/vk/glsl.h"
MString *pgraph_glsl_gen_psh_ready_vk2d(const PshState*,GenPshGlslOptions);
bool pgraph_glsl_psh_ready_vk2d_controls(const PshState*,uint32_t[8]);
#define CHECK(x) do { VkResult result=(x); if(result) { fprintf(stderr,"Vulkan %d at %d\n",result,__LINE__);abort(); } } while(0)
/* Test-only attribution; never linked into the core. Raw wall times are
 * not isolated GPU/driver-cold qualification or presentation measurements. */
static uint64_t wall_ns(void){
#ifdef _WIN32
 LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);
 return (uint64_t)((long double)t.QuadPart*1000000000.0L/f.QuadPart);
#else
 struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
#endif
}
static VkDevice device;
/* Numerical runs reuse driver stage compilation; opt-in timings stay uncached. */
static VkPipelineCache numerical_cache;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
typedef struct { VkBuffer handle;VkDeviceMemory memory;uint8_t *mapped; } Buffer;
static Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage) {
 Buffer b={0};VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
 CHECK(vkCreateBuffer(device,&ci,NULL,&b.handle));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.handle,&req);
 unsigned type=0;for(;type<memory_properties.memoryTypeCount;type++) if((req.memoryTypeBits&(1u<<type)) &&
 (memory_properties.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
 assert(type<memory_properties.memoryTypeCount);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 CHECK(vkAllocateMemory(device,&ai,NULL,&b.memory));CHECK(vkBindBufferMemory(device,b.handle,b.memory,0));
 CHECK(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,(void **)&b.mapped));return b;
}
static void drop(Buffer b) {vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.handle,NULL);vkFreeMemory(device,b.memory,NULL);}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
 if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){errors++;fprintf(stderr,"%s\n",data->pMessage);}return VK_FALSE;
}

typedef struct {VkImage image;VkImageView view;VkDeviceMemory memory;} Image;
static Image image_shape_size(VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,unsigned levels,unsigned kind,unsigned side,const VkComponentMapping *channels){
 Image a={0};VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=kind==2?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,.imageType=kind==1?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D,.format=format,.extent={side,side,kind==1?side:1},.mipLevels=levels,.arrayLayers=kind==2?6:1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
 CHECK(vkCreateImage(device,&ci,NULL,&a.image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,a.image,&req);unsigned type=0;while(!(req.memoryTypeBits&(1u<<type)))type++;assert(type<memory_properties.memoryTypeCount);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};CHECK(vkAllocateMemory(device,&ai,NULL,&a.memory));CHECK(vkBindImageMemory(device,a.image,a.memory,0));
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=a.image,.viewType=kind==1?VK_IMAGE_VIEW_TYPE_3D:kind==2?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D,.format=format,.components=channels?*channels:(VkComponentMapping){0},.subresourceRange={aspect,0,levels,0,kind==2?6:1}};CHECK(vkCreateImageView(device,&vi,NULL,&a.view));return a;
}
static Image image_shape(VkFormat f,VkImageUsageFlags u,VkImageAspectFlags a,unsigned levels,unsigned kind){return image_shape_size(f,u,a,levels,kind,4,NULL);}
static Image image_levels(VkFormat f,VkImageUsageFlags u,VkImageAspectFlags a,unsigned levels){return image_shape(f,u,a,levels,0);}
static Image image(VkFormat f,VkImageUsageFlags u,VkImageAspectFlags a){return image_levels(f,u,a,1);}
static void drop_image(Image a){vkDestroyImageView(device,a.view,NULL);vkDestroyImage(device,a.image,NULL);vkFreeMemory(device,a.memory,NULL);}
static VkShaderModule module(const GlslcResult *a){VkShaderModule m;VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=a->num_words*4,.pCode=a->words};CHECK(vkCreateShaderModule(device,&ci,NULL,&m));return m;}
static void release(GlslcResult *a){glslc_result_free_words(a);free(a->uniforms.members);free(a->push_constants.members);}
static void pack(uint8_t *dst,const GlslcLayout *layout,const PshUniformValues *values,const uint32_t controls[8]){
 ShaderUniformLayout final={.uniforms=layout->members,.num_uniforms=layout->count,.total_size=layout->size,.allocation=dst};
 for(size_t j=0;j<layout->count;j++){
  const GlslcUniform*u=&layout->members[j];
  if(!strcmp(u->name,"fsControl")){assert(controls&&u->offset==1616&&u->stride==16&&u->dim_a==2&&u->dim_v==4);uniform_update(&final,j+1,controls,32,true);continue;}
  unsigned k=0;while(k<PshUniform__COUNT&&strcmp(PshUniformInfo[k].name,u->name))k++;assert(k<PshUniform__COUNT);
  const UniformInfo*info=&PshUniformInfo[k];uniform_update(&final,j+1,(const uint8_t*)values+info->val_offs,info->size*info->count,true);
 }
}
static VkShaderModule vertex(bool smooth,unsigned family){
 MString *s=mstring_from_str("#version 450\n");pgraph_glsl_get_vtx_header(s,true,smooth,false,false,false);
 mstring_append(s,"layout(push_constant) uniform Vertex {vec4 d0;vec4 d1;vec4 b0;vec4 b1;vec4 p0;vec4 p1;vec4 p2;vec4 extra;} v;\nvoid main(){int i=gl_VertexIndex;if(v.extra.z<0.0 && i!=0)i=3-i;vec2 p=i==0?vec2(-1.0):i==1?vec2(3.0,-1.0):vec2(-1.0,3.0);gl_Position=v.extra.w<0.0?vec4(0.0,0.0,0.5,1.0):vec4(p,0.5,1.0);gl_PointSize=v.extra.w<0.0?4.0:1.0;vtxD0=v.d0+float(i)*vec4(0.125,-0.0625,0.25,0.0625);vtxD1=v.d1+float(i)*vec4(-0.125,0.25,0.0625,-0.0625);vtxB0=v.b0+float(i)*vec4(0.0625,0.125,-0.125,0.0625);vtxB1=v.b1+float(i)*vec4(-0.25,0.0625,0.125,-0.0625);vtxFog=v.extra.x+float(i)*0.125;vtxPos0=v.p0;vtxPos1=v.p1;vtxPos2=v.p2;triMZ=v.extra.y;vtxT0=vec4(0.0);vtxT1=vec4(0.0);vtxT2=vec4(0.0);vtxT3=vec4(0.0);if(v.extra.w!=0.0){int k=int(abs(v.extra.w))-1;float q=(k%8)==0?1.0:(k%8)==1?0.5:(k%8)==2?-0.5:(k%8)==3?0.0:(k%8)==4?1e-20:(k%8)==5?2.0:(k%8)==6?1.25:3.0;vec2 uv=(p+vec2(1.0))*0.5;float m=float((k/8)%4+1);vtxT0=vec4(uv*m-vec2(0.25),0.0,q);vtxT1=vec4(uv.yx*m+vec2(0.125),0.25,q);vtxT2=vec4(-uv*m+vec2(0.75),-0.25,q);vtxT3=vec4(uv*m-vec2(0.5),0.5,q); }\n");
 if(family)mstring_append(s,"int k=int(abs(v.extra.w))-128;int style=k%16;vec2 uv=(p+vec2(1.0))*0.5;vec2 m=(uv*2.0-vec2(1.0))*0.25;vec3 d; if(style==0)d=vec3(1.0,m.x,m.y);else if(style==1)d=vec3(-1.0,m.x,m.y);else if(style==2)d=vec3(m.x,1.0,m.y);else if(style==3)d=vec3(m.x,-1.0,m.y);else if(style==4)d=vec3(m.x,m.y,1.0);else if(style==5)d=vec3(m.x,m.y,-1.0);else if(style==6)d=vec3(1.0,1.0,0.0);else if(style==7)d=vec3(0.0,1.0,1.0);else if(style==8)d=vec3(1.0,0.0,1.0);else if(style==9)d=vec3(0.0);else if(style==10)d=vec3(1.0);else if(style==11)d=vec3(-0.0,0.0,0.0);else d=vec3(uv*1.5+vec2(0.125),float(k%4)*0.25+uv.x*0.125);int qi=(k/16)%8;float q=qi==0?1.0:qi==1?0.5:qi==2?2.0:qi==3?-0.5:qi==4?0.0:qi==5?1e-20:qi==6?1.25:3.0;vtxT0=vec4(d,q);vtxT1=vec4(d.yzx,q);vtxT2=vec4(-d,q);vtxT3=vec4(d.zxy,q);\n");
 if(family>=2)mstring_append(s,"int sk=int(abs(v.extra.w))-256;float sq=(sk%8)==0?1.0:(sk%8)==1?0.5:(sk%8)==2?2.0:(sk%8)==3?-0.5:(sk%8)==4?0.0:(sk%8)==5?1e-20:(sk%8)==6?1.25:3.0;vec2 su=uv+vec2(0.125);float sr=v.extra.z>0.0?uv.x:v.extra.y;vtxT0=vec4(su,sr*v.d0.x*sq,sq);vtxT1=vec4(su.yx,sr*v.d0.y*sq,sq);vtxT2=vec4(-su+vec2(1.0),sr*v.d0.z*sq,sq);vtxT3=vec4(su*2.0,sr*v.d0.w*sq,sq);\n");
 if(family==3)mstring_append(s,"triMZ=v.d1.x;\n");
 mstring_append(s,"}\n");
 GlslcResult r;if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(s),NULL,&r)){fprintf(stderr,"vertex %d %s\n",r.error_line,r.error);abort();}VkShaderModule m=module(&r);release(&r);mstring_unref(s);return m;
}
static Image dimensional_texture(unsigned kind,VkCommandBuffer cmd,VkQueue queue,VkFence fence){
 Image a=image_shape(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,3,kind);
 Buffer staging=buffer(kind==1?1168:2016,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);float *data=(float *)staging.mapped;VkBufferImageCopy levels[3];unsigned texel=0;
 for(unsigned level=0;level<3;level++){
  unsigned side=4u>>level,depth=kind==1?side:1,layers=kind==2?6:1;
  levels[level]=(VkBufferImageCopy){.bufferOffset=texel*16,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,layers},.imageExtent={side,side,depth}};
  for(unsigned face=0;face<layers;face++)for(unsigned z=0;z<depth;z++)for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){
   data[texel*4]=kind==2?(face+1)*.125f:(x+1)*.125f;data[texel*4+1]=(y+level)*.125f;data[texel*4+2]=kind==2?(x+1)*.125f:(z+level+1)*.125f;data[texel*4+3]=(x+y+z+face+level)%3?.5f:0;
  }
 }
 assert(texel*16==(kind==1?1168:2016));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=a.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,kind==2?6:1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);vkCmdCopyBufferToImage(cmd,staging.handle,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,levels);
 b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(staging);return a;
}
static VkImageView depth_marker_view(Image source,unsigned format){
 VkImageView view;VkImageViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=source.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.components={VK_COMPONENT_SWIZZLE_R,format==1?VK_COMPONENT_SWIZZLE_ONE:VK_COMPONENT_SWIZZLE_ZERO,format==2?VK_COMPONENT_SWIZZLE_ONE:VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE},.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1}};CHECK(vkCreateImageView(device,&ci,NULL,&view));return view;
}
static Image packed_depth_texture(VkCommandBuffer cmd,VkQueue queue,VkFence fence){
 Image a=image_shape(VK_FORMAT_R32_UINT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,3,0);Buffer staging=buffer(84,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);uint32_t *data=(uint32_t *)staging.mapped;unsigned texel=0;VkBufferImageCopy levels[3];
 for(unsigned level=0;level<3;level++){
  unsigned side=4u>>level;levels[level]=(VkBufferImageCopy){.bufferOffset=texel*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={side,side,1}};
  for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){unsigned depth=(texel%7)==0?0:(texel%7)==1?0xffffffu:(0x12345u*(texel+1))&0xffffffu;data[texel]=(depth<<8)|((texel*13)&255);}
 }
 assert(texel==21);VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=a.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);vkCmdCopyBufferToImage(cmd,staging.handle,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,levels);
 b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(staging);return a;
}
static Image encoded_f16_texture(VkCommandBuffer cmd,VkQueue queue,VkFence fence){
 Image a=image_shape(VK_FORMAT_R16_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,3,0);Buffer staging=buffer(42,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);uint16_t *data=(uint16_t *)staging.mapped;unsigned texel=0;VkBufferImageCopy levels[3];
 for(unsigned level=0;level<3;level++){
  unsigned side=4u>>level;levels[level]=(VkBufferImageCopy){.bufferOffset=texel*2,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={side,side,1}};
  for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){static const uint16_t bits[]={0,1,0x7ff,0x800,0x7fff,0x8000,0xfffe,0xffff};data[texel]=bits[(texel*5)%8];}
 }
 assert(texel==21);VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=a.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);vkCmdCopyBufferToImage(cmd,staging.handle,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,levels);
 b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(staging);return a;
}
static Image signed_gb_texture(VkCommandBuffer cmd,VkQueue queue,VkFence fence){
 VkComponentMapping channels={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G};Image a=image_shape_size(VK_FORMAT_R8G8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,9,0,256,&channels);Buffer staging=buffer(174762,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);uint8_t *data=(uint8_t *)staging.mapped;unsigned texel=0;VkBufferImageCopy levels[9];
 for(unsigned level=0;level<9;level++){
  unsigned side=256u>>level;levels[level]=(VkBufferImageCopy){.bufferOffset=texel*2,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={side,side,1}};
  for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){static const uint8_t bits[]={0,1,126,127,128,129,254,255};data[texel*2]=bits[(x*3+y*5+level)%8];data[texel*2+1]=bits[(x*5+y*3+level*2)%8];}
 }
 assert(texel==87381);VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=a.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,9,0,1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);vkCmdCopyBufferToImage(cmd,staging.handle,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,9,levels);
 b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(staging);return a;
}
static unsigned fixture_kind(const PshState *s,unsigned j){unsigned mode=(s->shader_stage_program>>(j*5))&31;if(s->tex_x8y24[j] && (s->shadow_map[j] || mode==PS_TEXTUREMODES_PROJECT3D || (mode==PS_TEXTUREMODES_PROJECT2D && s->dim_tex[j]==2)))return 3;return (mode==PS_TEXTUREMODES_CUBEMAP || mode==PS_TEXTUREMODES_DOT_RFLCT_DIFF || mode==PS_TEXTUREMODES_DOT_RFLCT_SPEC || mode==PS_TEXTUREMODES_DOT_STR_CUBE)?(s->tex_cubemap[j]?2:0):s->dim_tex[j]==3?1:mode==PS_TEXTUREMODES_PROJECT2D && s->tex_cubemap[j]?2:0;}
static void fixture_dead_meta(PshState *s,unsigned i){
 s->g8b8_mip_flags=(i*17+1)&255;s->g8b8_mip_clamp_u=(i*13+7)&255;s->g8b8_mip_clamp_v=(i*19+11)&255;s->g8b8_mip_mirror=(i*23+3)&255;
 for(unsigned j=0;j<4;j++){
  unsigned mode=(s->shader_stage_program>>(j*5))&31;
  if(mode==PS_TEXTUREMODES_NONE || mode==PS_TEXTUREMODES_PASSTHRU || mode==PS_TEXTUREMODES_CLIPPLANE || mode==PS_TEXTUREMODES_DOTPRODUCT || mode==PS_TEXTUREMODES_DOT_ZW){
   s->tex_x8y24[j]=true;s->shadow_map[j]=1+(i+j*17)%255;s->dim_tex[j]=(int)((i+j)%9)-2;s->tex_cubemap[j]=!!((i+j)&1);s->conv_tex[j]=99;s->colorkey_mode[j]=99;s->alphakill[j]=!!((i+j)&1);
   if(mode!=PS_TEXTUREMODES_PASSTHRU)s->border_adjust[j]=!!((i+j)&2);
   if(!(s->point_sprite && j==3))s->rect_tex[j]=!!((i+j)&4);
  }
 }
}
static void fixture_family(PshState *s,unsigned unit,unsigned family){
 unsigned mode=family<1?PS_TEXTUREMODES_PROJECT2D:family<3?PS_TEXTUREMODES_PROJECT3D:family==3?PS_TEXTUREMODES_PROJECT2D:PS_TEXTUREMODES_CUBEMAP;
 s->shader_stage_program=(s->shader_stage_program&~(31u<<(unit*5)))|(mode<<(unit*5));s->dim_tex[unit]=family<2?3:2;s->tex_cubemap[unit]=family==3 || family==4;
}
static uint32_t encoded_inputs(unsigned seed,bool alpha,bool ef){
 const unsigned regs[]={0,1,2,3,4,5,8,9,10,11,12,13,14,15};uint32_t v=0;
 for(unsigned j=0;j<4;j++){unsigned r=regs[(seed+j*5)%(ef?14:13)],mapping=alpha?((seed+j)&1?0x20:0):((seed+j*3)&7)<<5,channel=alpha?16:((seed>>j)&1?16:0);v=(v<<8)|r|mapping|channel;}return v;
}
static uint32_t regular_inputs(unsigned seed,bool alpha){
 const unsigned regs[]={0,1,2,3,4,5,8,9,10,11,12,13};uint32_t value=0;
 for(unsigned j=0;j<4;j++){unsigned reg=regs[(seed+j*5)%12],mapping=alpha?((seed+j)&1?32:0):((seed+j*3)&7)<<5,channel=alpha?16:((seed>>j)&1?16:0);value=(value<<8)|reg|mapping|channel;}
 return value;
}
static uint32_t dynamic_inputs(unsigned seed,bool alpha,bool signed_alpha){
 uint32_t v=0;for(unsigned j=0;j<4;j++){
  unsigned reg=(seed+j*5)&15,mapping=alpha && !signed_alpha?((seed+j)&1):((seed+j*3)&7);
  v=(v<<8)|reg|(mapping<<5)|(alpha || ((seed>>j)&1)?16:0);
 }return v;
}
static void verify_coverage(unsigned first,unsigned limit,unsigned written,unsigned discarded,unsigned sample_pixels,unsigned point_pixels,unsigned metadata_signal,unsigned sampler_signal,unsigned composition_signal,unsigned alias_signal){
 if(first>=74752 && first<76288){assert(metadata_signal);printf("Float metadata fixture: %u nontrivial pixels; production GLSL identical with packed flag off/on\n",metadata_signal);}
 if(first>=77824 && first<79360){assert(sampler_signal);printf("Packed sampler fixture: %u nontrivial numeric-color pixels\n",sampler_signal);}
 if(first>=79360 && first<80656){assert(composition_signal);printf("Composition fixture: %u nontrivial four-stage pixels\n",composition_signal);}
 if(first>=80656){assert(alias_signal && written==(limit-first)*16 && !discarded);printf("Aliased combiner fixture: %u nontrivial pixels, every draw present\n",alias_signal);}
 if(first>=16896 && first<80656)assert(written && sample_pixels);
 if(first>=4096 && (first<10240 || (first>=14336 && first<16384)))assert(written && discarded && sample_pixels);
 if(first>=8192 && (first<10240 || (first>=14336 && first<16384)))assert(point_pixels);
}
static bool has_layer(const char *name){uint32_t n=0;CHECK(vkEnumerateInstanceLayerProperties(&n,NULL));VkLayerProperties *p=calloc(n,sizeof(*p));CHECK(vkEnumerateInstanceLayerProperties(&n,p));bool found=false;for(unsigned i=0;i<n;i++)if(!strcmp(p[i].layerName,name))found=true;free(p);return found;}
static bool has_extension(const char *name){uint32_t n=0;CHECK(vkEnumerateInstanceExtensionProperties(NULL,&n,NULL));VkExtensionProperties *p=calloc(n,sizeof(*p));CHECK(vkEnumerateInstanceExtensionProperties(NULL,&n,p));bool found=false;for(unsigned i=0;i<n;i++)if(!strcmp(p[i].extensionName,name))found=true;free(p);return found;}
static bool has_layer_extension(const char *layer,const char *name){uint32_t n=0;CHECK(vkEnumerateInstanceExtensionProperties(layer,&n,NULL));VkExtensionProperties *p=calloc(n,sizeof(*p));CHECK(vkEnumerateInstanceExtensionProperties(layer,&n,p));bool found=false;for(unsigned i=0;i<n;i++)if(!strcmp(p[i].extensionName,name))found=true;free(p);return found;}
int main(int argc,char**argv){
 assert(argc==3);unsigned first=strtoul(argv[1],NULL,10),limit=strtoul(argv[2],NULL,10);assert(first<limit&&limit<=9984);
 const char*fault=getenv("READY2D_FAULT");if(fault)assert(!strcmp(fault,"mode")||!strcmp(fault,"raw")||!strcmp(fault,"rect"));
 bool validated=true;const char*layer="VK_LAYER_KHRONOS_validation",*debug=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 if(!has_layer(layer)||!has_extension(debug)||!(has_extension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)||has_layer_extension(layer,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))){puts("UNQUALIFIED: CORE+SYNC validation unavailable");return 77;}
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
 VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};
 VkValidationFeatureEnableEXT sf=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;VkValidationFeaturesEXT vf={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.pNext=&mi,.enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&sf};const char*extensions[]={debug,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pNext=&vf,.pApplicationInfo=&app,.enabledLayerCount=1,.ppEnabledLayerNames=&layer,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
 PFN_vkCreateDebugUtilsMessengerEXT create=(void*)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");VkDebugUtilsMessengerEXT messenger;CHECK(create(instance,&mi,NULL,&messenger));
 uint32_t n=1;VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);
 VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13};vkGetPhysicalDeviceFeatures2(gpu,&features);if(!features.features.largePoints){puts("UNQUALIFIED: large points unavailable");return 77;}if(!f13.shaderDemoteToHelperInvocation){puts("UNQUALIFIED: shader demote unavailable");return 77;}
 uint32_t qn=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&qn,families);unsigned family=0;while(family<qn && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<qn);
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};f13=(VkPhysicalDeviceVulkan13Features){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.shaderDemoteToHelperInvocation=VK_TRUE};VkPhysicalDeviceFeatures enabled={.largePoints=VK_TRUE};VkDeviceCreateInfo dci={.pEnabledFeatures=&enabled,.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f13,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};CHECK(vkCreateDevice(gpu,&dci,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 Buffer uniform=buffer(2048,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),readback=buffer(320,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 Image color=image(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT),depth=image(VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
 VkAttachmentDescription attachments[2]={{.format=VK_FORMAT_R32G32B32A32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},{.format=VK_FORMAT_D32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}};
 VkAttachmentReference cr={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&cr,.pDepthStencilAttachment=&dr};
 VkSubpassDependency deps[2]={{.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},{.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT}};
 VkRenderPassCreateInfo rci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=2,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=2,.pDependencies=deps};VkRenderPass pass;CHECK(vkCreateRenderPass(device,&rci,NULL,&pass));VkImageView views[]={color.view,depth.view};VkFramebufferCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=2,.pAttachments=views,.width=4,.height=4,.layers=1};VkFramebuffer fb;CHECK(vkCreateFramebuffer(device,&fci,NULL,&fb));
 VkDescriptorSetLayoutBinding binding[5]={{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};for(unsigned j=0;j<4;j++)binding[j+1]=(VkDescriptorSetLayoutBinding){.binding=2+j,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};VkDescriptorSetLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=binding};VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&lci,NULL,&set_layout));VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT,0,128};VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&plci,NULL,&layout));
 VkDescriptorPoolSize pool_size[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4}};VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=pool_size};VkDescriptorPool pool;CHECK(vkCreateDescriptorPool(device,&pci,NULL,&pool));VkDescriptorSetAllocateInfo sai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&sai,&set));VkDescriptorBufferInfo bi={uniform.handle,0,2048};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&bi};vkUpdateDescriptorSets(device,1,&write,0,NULL);
 VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool commands;CHECK(vkCreateCommandPool(device,&cpci,NULL,&commands));VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&cai,&cmd));VkFenceCreateInfo fence_ci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fence_ci,NULL,&fence));
 Image texture=image_levels(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,3);
 Buffer upload=buffer(336,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);float *pixels=(float *)upload.mapped;unsigned texel=0;VkBufferImageCopy levels[3]={0};
 for(unsigned level=0;level<3;level++){
  unsigned side=4u>>level;levels[level]=(VkBufferImageCopy){.bufferOffset=texel*16,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={side,side,1}};
  for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){
   pixels[texel*4]=(x+level+1)*.125f;pixels[texel*4+1]=(y+level)*.25f;pixels[texel*4+2]=((x^y)+level)*.125f;pixels[texel*4+3]=(x+y+level)%3?.5f:0;
  }
 }
 VkCommandBufferBeginInfo upload_begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&upload_begin));
 VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=texture.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);vkCmdCopyBufferToImage(cmd,upload.handle,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,levels);
 barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo upload_submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&upload_submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(upload);
 VkImageView stage_views[4]={texture.view,0,0,0};
 for(unsigned j=1;j<4;j++){VkComponentSwizzle c[3]={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B};VkComponentMapping mapping={c[j%3],c[(j+1)%3],c[(j+2)%3],VK_COMPONENT_SWIZZLE_A};if(j==3)mapping.r=VK_COMPONENT_SWIZZLE_ONE;VkImageViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=texture.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.components=mapping,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1}};CHECK(vkCreateImageView(device,&ci,NULL,&stage_views[j]));}
 VkSampler samplers[16];
 for(unsigned j=0;j<16;j++){
  VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=(j&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=(j&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.mipmapMode=(j&2)?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=(VkSamplerAddressMode)(j>>2),.addressModeV=(VkSamplerAddressMode)(j>>2),.addressModeW=(VkSamplerAddressMode)(j>>2),.maxLod=2,.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};CHECK(vkCreateSampler(device,&sci,NULL,&samplers[j]));
 }
 Image volume=dimensional_texture(1,cmd,queue,fence),cube=dimensional_texture(2,cmd,queue,fence);
 Image signed_gb={0};if(limit>54272)signed_gb=signed_gb_texture(cmd,queue,fence);Image encoded_f16={0};if(limit>48128)encoded_f16=encoded_f16_texture(cmd,queue,fence);VkImageView depth_views[3]={0};Image packed_depth={0};if(limit>37888){for(unsigned j=0;j<3;j++)depth_views[j]=depth_marker_view(texture,j);packed_depth=packed_depth_texture(cmd,queue,fence);}
 VkShaderModule vertices[4][2]={{vertex(false,0),vertex(true,0)},{0,0},{0,0},{0,0}};unsigned failures=0,compared=0,written=0,discarded=0,sample_pixels=0;uint64_t words[2]={0},reference_source_bytes=0;bool timings=false;GlslcResult bank[8]={0};uint64_t a_reference=0,a_candidate=0;unsigned oracle_none=0,aba=0,declined=0;
 for(unsigned test=first;test<limit;test++){
  bool legal_sum=test>=9216;unsigned recipe=test;
  if(legal_sum){unsigned q=test-9216,cy=q%3;q/=3;unsigned sc=3+4*(q%2);q/=2;recipe=6144+(q*8+sc)*3+cy;}
  bool negative_regular=recipe>=3072&&recipe<6144;bool supplemental=recipe>=3072;unsigned n=recipe%3072,cycle=n%3;n/=3;unsigned scenario=n%8;n/=8;unsigned rect=n%2;n/=2;unsigned mask=n%16;n/=16;unsigned helper=n%2;n/=2;unsigned smooth=n%2;assert(n/2==0);bool negative=negative_regular||(!legal_sum&&recipe>=6144&&scenario%4==3);
  unsigned active=cycle==1?(mask^1):mask,unit=(scenario+cycle%2)%4;
  PshState state={.smooth_shading=smooth,.depth_format=helper?DEPTH_FORMAT_HELPER_LIVENESS:DEPTH_FORMAT_D24,.surface_zeta_format=NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,.depth_clipping=true,.final_inputs_0=0x201c0000,.final_inputs_1=0x1c00};
  for(unsigned j=0;j<4;j++){state.shader_stage_program|=((active>>j)&1)<<(5*j);state.dim_tex[j]=(active>>j)&1?2:0;state.rect_tex[j]=rect&&((active>>j)&1);state.alphakill[j]=scenario==3||scenario==7;}
  if(scenario==1||scenario==3||scenario==5||scenario==6||scenario==7){unsigned reg=PS_REGISTER_T0+unit;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;}
  if(scenario==2||scenario==7){state.combiner_control=1;state.rgb_inputs[0]=0x20082004u;state.alpha_inputs[0]=0x30183014u;state.rgb_outputs[0]=0x00000c00;state.alpha_outputs[0]=0x00000c00;state.final_inputs_0=0x200c0000;state.final_inputs_1=0x1c00;}
  if(supplemental){
   state.combiner_control=8|((PS_COMBINERCOUNT_MUX_MSB|((mask&2)?PS_COMBINERCOUNT_UNIQUE_C0:0)|((mask&4)?PS_COMBINERCOUNT_UNIQUE_C1:0))<<8);
   unsigned seed=(mask*37+scenario*11+cycle%2*17);
   for(unsigned j=0;j<8;j++){state.rgb_inputs[j]=negative_regular?encoded_inputs(seed+j,false,false):regular_inputs(seed+j,false);state.alpha_inputs[j]=negative_regular?encoded_inputs(seed+j+5,true,false):regular_inputs(seed+j+5,true);state.rgb_outputs[j]=(((seed+j)&255)<<12)|(12<<8)|(13<<4)|5;state.alpha_outputs[j]=(((seed+j)&4)<<12)|(13<<8)|(12<<4)|5;}
   unsigned final_regs[]={PS_REGISTER_R0,PS_REGISTER_V1,PS_REGISTER_FOG,negative_regular?PS_REGISTER_T0:PS_REGISTER_V1R0_SUM};unsigned reg=final_regs[scenario%4];state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=((legal_sum?PS_REGISTER_R0:reg)|16u)<<8;
  }
  state.alpha_test=scenario==4||scenario==7;unsigned alpha_func=scenario==7?4:((mask+cycle%2)%8);
  PshUniformValues values={0};values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=16777215;values.clipRange[0][2]=scenario==6||scenario==7?125:0;values.clipRange[0][3]=scenario==6||scenario==7?225:16777215;values.alphaFunc[0]=alpha_func;values.alphaRef[0]=128;values.alphaTestEnable[0]=state.alpha_test;
  values.combCtl[0]=state.combiner_control;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
  if(supplemental){for(unsigned j=0;j<18;j++){for(unsigned k=0;k<4;k++){values.consts[j][k]=((j*5+k*3+scenario)%9)*.125f;}}values.fogColor[0][0]=.125f;values.fogColor[0][1]=.375f;values.fogColor[0][2]=.625f;values.twoSideSel[0]=scenario&1;}
  for(unsigned j=0;j<8;j++){values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];values.clipRegion[j][0]=scenario==5||scenario==7?1:0;values.clipRegion[j][1]=scenario==5||scenario==7?1:0;values.clipRegion[j][2]=scenario==5||scenario==7?3:4;values.clipRegion[j][3]=scenario==5||scenario==7?3:4;}
  for(unsigned j=0;j<4;j++){values.texScale[j]=.5f+(j%3)*.5f;if(state.alphakill[j])values.alphaKillMask[0]|=1u<<j;VkDescriptorImageInfo info={samplers[scenario==3||scenario==7?3:0],stage_views[j],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);}
  float data[8][4]={{.125f,.25f,.5f,.75f},{.5f,.25f,.125f,.25f},{.75f,.5f,.25f,.125f},{.25f,.5f,.75f,.875f},{0,0,100,1},{4,0,200,1},{0,4,300,1},{.5f,0,1,1}};if(supplemental)data[7][2]=scenario&2?-1:1;data[7][3]=(float)(1+((scenario+mask)%4))+(cycle==1?4:0);
  uint32_t controls[8];if(negative){memset(controls,0xa5,32);uint32_t before[8];memcpy(before,controls,32);assert(!pgraph_glsl_psh_ready_vk2d_controls(&state,controls));assert(!memcmp(before,controls,32));assert(!pgraph_glsl_gen_psh_ready_vk2d(&state,(GenPshGlslOptions){.vulkan=true,.ubo_binding=1,.tex_binding=2}));declined++;continue;}
  assert(pgraph_glsl_psh_ready_vk2d_controls(&state,controls));assert(controls[1]==state.shader_stage_program);for(unsigned j=0;j<4;j++)assert(controls[4+j]==state.rect_tex[j]);
  GlslcResult modules[2]={0};GlslcOptions go={.spirv_1_6=!!(scenario&1),.emit_layout=true};GenPshGlslOptions options={.vulkan=true,.locations=true,.ubo_binding=1,.tex_binding=2};MString*source=pgraph_glsl_gen_psh(&state,options);assert(glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(source),&go,&modules[0]));reference_source_bytes+=strlen(mstring_get_str(source));mstring_unref(source);
  unsigned idx=smooth|(helper<<1)|((unsigned)go.spirv_1_6<<2);if(!bank[idx].words){source=pgraph_glsl_gen_psh_ready_vk2d(&state,options);assert(source);assert(glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(source),&go,&bank[idx]));mstring_unref(source);}modules[1]=bank[idx];
  words[0]+=modules[0].num_words;words[1]+=modules[1].num_words;
  uint32_t expected[80];
  for(unsigned path=0;path<2;path++){
   assert(modules[path].uniforms.size<=2048);memset(uniform.mapped,0,2048);PshUniformValues actual_values=values;uint32_t actual_controls[8];memcpy(actual_controls,controls,32);if(path&&fault){if(!strcmp(fault,"mode"))actual_controls[1]=0;if(!strcmp(fault,"rect"))for(unsigned k=4;k<8;k++)actual_controls[k]^=1;if(!strcmp(fault,"raw"))actual_values.combFinal0[0]=actual_values.combFinal1[0]=0;}pack(uniform.mapped,&modules[path].uniforms,&actual_values,path?actual_controls:NULL);VkShaderModule frag=module(&modules[path]);
   VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertices[0][state.smooth_shading],.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
   VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=state.point_sprite?VK_PRIMITIVE_TOPOLOGY_POINT_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkViewport viewport={0,0,4,4,0,1};VkRect2D scissor={{0,0},{4,4}};VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_ALWAYS};VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,.pColorBlendState=&cb,.layout=layout,.renderPass=pass};VkPipeline pipeline;if(!timings && !numerical_cache){VkPipelineCacheCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};CHECK(vkCreatePipelineCache(device,&ci,NULL,&numerical_cache));}uint64_t build_start=timings?wall_ns():0;CHECK(vkCreateGraphicsPipelines(device,timings?VK_NULL_HANDLE:numerical_cache,1,&gp,NULL,&pipeline));(void)build_start;
   CHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));VkClearValue clear[2]={{{{-2,3,5,7}}}, {.depthStencil={1,0}}};VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{4,4}},.clearValueCount=2,.pClearValues=clear};vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,128,data);vkCmdDraw(cmd,state.point_sprite?1:3,1,0,0);vkCmdEndRenderPass(cmd);
   VkBufferImageCopy copies[2]={{.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}},{.bufferOffset=256,.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={4,4,1}}};vkCmdCopyImageToBuffer(cmd,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,copies);vkCmdCopyImageToBuffer(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,copies+1);CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};uint64_t draw_start=timings?wall_ns():0;CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));(void)draw_start;
   if(!path)memcpy(expected,readback.mapped,320);else for(unsigned j=0;j<80;j++){uint32_t got;float x,y;memcpy(&got,readback.mapped+j*4,4);memcpy(&x,&expected[j],4);memcpy(&y,&got,4);compared++;if(got!=expected[j] && !(isnan(x)&&isnan(y))){if(failures++<25)fprintf(stderr,"case %u output %u: %08x (%g) != %08x (%g)\n",test,j,expected[j],x,got,y);}}
   vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,frag,NULL);
  }
  uint64_t hr=1469598103934665603ull,hc=hr;for(unsigned j=0;j<80;j++){hr=(hr^expected[j])*1099511628211ull;uint32_t v;memcpy(&v,readback.mapped+j*4,4);hc=(hc^v)*1099511628211ull;}
  if(cycle==0){a_reference=hr;a_candidate=hc;}if(cycle==2&&test>=first+2){assert(hr==a_reference);if(!fault)assert(hc==a_candidate);aba++;}
  if(!supplemental&&scenario==0&&active==0){for(unsigned j=0;j<64;j++)assert(expected[j]==0x3f800000u);oracle_none++;}
  for(unsigned j=0;j<16;j++){if(expected[j*4]==0xc0000000u)discarded++;else{written++;sample_pixels+=active!=0;}}
  release(&modules[0]);
 }
 CHECK(vkResetCommandBuffer(cmd,0));
 for(unsigned k=0;k<8;k++)if(bank[k].words)release(&bank[k]);
 assert(compared==((limit-first)-declined)*80);printf("NEGATIVE_ADMISSION %u\n",declined);
 printf("READY2D_LEDGER first=%u limit=%u cases=%u scalars=%u written=%u discarded=%u sampled=%u none_oracles=%u aba=%u CORE=enabled SYNC=enabled fault=%s\n",first,limit,limit-first,compared,written,discarded,sample_pixels,oracle_none,aba,fault?fault:"none");
 printf("SOURCE_WORK words=%llu/%llu GLSLbytes=%llu; no timing claim\n",(unsigned long long)words[0],(unsigned long long)words[1],(unsigned long long)reference_source_bytes);
 printf("Fragment GPU: %u scalar color/depth/discard results, %u differences, %u validation errors; validation=%s\n",compared,failures,errors,validated?"enabled":"DISABLED");
 for(unsigned j=0;j<4;j++){for(unsigned k=0;k<2;k++)if(vertices[j][k])vkDestroyShaderModule(device,vertices[j][k],NULL);}
 vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,commands,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);drop_image(color);drop_image(depth);drop(uniform);drop(readback);for(unsigned j=0;j<16;j++)vkDestroySampler(device,samplers[j],NULL);for(unsigned j=0;j<3;j++)if(depth_views[j])vkDestroyImageView(device,depth_views[j],NULL);if(signed_gb.image)drop_image(signed_gb);if(encoded_f16.image)drop_image(encoded_f16);if(packed_depth.image)drop_image(packed_depth);for(unsigned j=1;j<4;j++)vkDestroyImageView(device,stage_views[j],NULL);drop_image(texture);drop_image(volume);drop_image(cube);if(numerical_cache)vkDestroyPipelineCache(device,numerical_cache,NULL);vkDestroyDevice(device,NULL);if(validated)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);return failures||errors?1:0;
}
