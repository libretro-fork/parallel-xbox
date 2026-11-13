/* SPDX-License-Identifier: GPL-2.0-or-later */
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
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"
#include "spv-psh-composition.h"
#include "spv-psh-alias.h"
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
static void pack(uint8_t *dst,const GlslcLayout *layout,const PshUniformValues *values){
 for(size_t j=0;j<layout->count;j++){const GlslcUniform *u=layout->members+j;unsigned k=0;while(k<PshUniform__COUNT && strcmp(PshUniformInfo[k].name,u->name))k++;assert(k<PshUniform__COUNT);const uint8_t *src=(const uint8_t *)values+PshUniformInfo[k].val_offs;for(size_t n=0;n<u->dim_a;n++)memcpy(dst+u->offset+n*u->stride,src+n*u->dim_v*4,u->dim_v*4);}
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
int main(int argc,char **argv){
 if(argc==12 && !strcmp(argv[1],"--verify-coverage")){
  unsigned c[10];for(unsigned i=0;i<10;i++){char *end;unsigned long v=strtoul(argv[i+2],&end,10);assert(*argv[i+2] && !*end && v<=UINT32_MAX);c[i]=v;}
  assert(c[0]<c[1] && c[1]<=82704);verify_coverage(c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7],c[8],c[9]);return 0;
 }
 bool defer_coverage=argc==4 && !strcmp(argv[3],"--defer-coverage");assert(argc==1 || argc==3 || defer_coverage);
 unsigned first=argc>=3?strtoul(argv[1],NULL,10):0,limit=argc>=3?strtoul(argv[2],NULL,10):64;assert(first<limit);printf("PSH fixtures [%u,%u)\n",first,limit);
 bool validated=getenv("NO_VALIDATION")==NULL;const char *layer="VK_LAYER_KHRONOS_validation",*debug=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledLayerCount=validated,.ppEnabledLayerNames=&layer,.enabledExtensionCount=validated,.ppEnabledExtensionNames=&debug};VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
 PFN_vkCreateDebugUtilsMessengerEXT create=(void *)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void *)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
 VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};if(validated)CHECK(create(instance,&mi,NULL,&messenger));
 uint32_t n=1;VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);
 VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13};vkGetPhysicalDeviceFeatures2(gpu,&features);if(!features.features.largePoints){puts("UNQUALIFIED: large points unavailable");return 2;}if(!f13.shaderDemoteToHelperInvocation){puts("UNQUALIFIED: shader demote unavailable");return 2;}
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
 VkSampler samplers[16];
 for(unsigned j=0;j<16;j++){
  VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=(j&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=(j&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.mipmapMode=(j&2)?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=(VkSamplerAddressMode)(j>>2),.addressModeV=(VkSamplerAddressMode)(j>>2),.addressModeW=(VkSamplerAddressMode)(j>>2),.maxLod=2,.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};CHECK(vkCreateSampler(device,&sci,NULL,&samplers[j]));
 }
 Image volume=dimensional_texture(1,cmd,queue,fence),cube=dimensional_texture(2,cmd,queue,fence);
 Image signed_gb={0};if(limit>54272)signed_gb=signed_gb_texture(cmd,queue,fence);Image encoded_f16={0};if(limit>48128)encoded_f16=encoded_f16_texture(cmd,queue,fence);VkImageView depth_views[3]={0};Image packed_depth={0};if(limit>37888){for(unsigned j=0;j<3;j++)depth_views[j]=depth_marker_view(texture,j);packed_depth=packed_depth_texture(cmd,queue,fence);}
 VkShaderModule vertices[4][2]={{vertex(false,0),vertex(true,0)},{vertex(false,1),vertex(true,1)},{0,0}};if(limit>37888){vertices[2][0]=vertex(false,2);vertices[2][1]=vertex(true,2);}if(limit>52224){vertices[3][0]=vertex(false,3);vertices[3][1]=vertex(true,3);}unsigned failures=0,compared=0,written=0,discarded=0,sample_pixels=0,point_pixels=0,metadata_signal=0,sampler_signal=0,composition_signal=0,alias_signal=0;uint64_t words[2]={0},reference_source_bytes=0;bool timings=getenv("PSH_COMPILER_TIMINGS")!=NULL;
 for(unsigned test=first;test<limit;test++){
  PshState state={.final_inputs_0=0x04200000,.final_inputs_1=0x1400};state.smooth_shading=!!(test&1);state.z_perspective=!!(test&2);state.depth_clipping=!!(test&4);state.window_clip_exclusive=!!(test&8);state.alpha_test=!!(test&16);state.alpha_dynamic=!!(test&32);state.depth_format=(test/64)%4;
  if(test>=512){
   unsigned f=test-512;state.combiner_control=(f%9) | (((f&1?PS_COMBINERCOUNT_MUX_MSB:0)|(f&2?PS_COMBINERCOUNT_UNIQUE_C0:0)|(f&4?PS_COMBINERCOUNT_UNIQUE_C1:0))<<8);
   for(unsigned j=0;j<(state.combiner_control&255);j++){
    state.rgb_inputs[j]=encoded_inputs(f+j,false,false);state.alpha_inputs[j]=encoded_inputs(f+j+5,true,false);
    state.rgb_outputs[j]=(((f+j)&255)<<12) | (12<<8) | (4<<4) | 5;
    state.alpha_outputs[j]=((f+j)&4)<<12 | (13<<8) | (12<<4) | 5;
   }
   if(f&8){state.final_inputs_0=encoded_inputs(f+7,false,true);uint32_t e=encoded_inputs(f+11,false,false),g=encoded_inputs(f+3,true,true);state.final_inputs_1=(e&0xffff0000u)|((g>>16)&0xff00u)|((f&7)<<5);}
  }
  PshUniformValues values={0};values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=state.depth_format==DEPTH_FORMAT_D16?65535:16777215;values.clipRange[0][2]=0;values.clipRange[0][3]=values.clipRange[0][1];values.alphaFunc[0]=test%8;values.alphaRef[0]=128;values.alphaTestEnable[0]=test&64;values.twoSideSel[0]=!!(test&128);
  values.fogColor[0][0]=.125f;values.fogColor[0][1]=.25f;values.fogColor[0][2]=.75f;
  for(unsigned j=0;j<18;j++)for(unsigned k=0;k<4;k++)values.consts[j][k]=k==3?.25f+(j%3)*.25f:((int)((j*7+k*3+test)%17)-8)*.125f;
  for(unsigned j=0;j<8;j++){values.clipRegion[j][0]=test&8?10:0;values.clipRegion[j][1]=0;values.clipRegion[j][2]=test&8?11:4;values.clipRegion[j][3]=4;}
  float data[8][4]={{.125f,.25f,.5f,.75f},{.5f,.25f,.125f,.25f},{.75f,.5f,.25f,.125f},{.25f,.5f,.75f,.875f},{0,0,100,1},{4,0,200,1},{0,4,300,1},{.5f,0,1,0}};data[7][2]=test&256?-1:1;
  if(test>=2048){
   unsigned f=test-2048,shape=(f/16)%8;
   values.surfaceScale[0][0]=1+(f%3);values.surfaceScale[0][1]=1+((f/3)%3);
   values.depthOffset[0]=(f&1)?.125f:-.25f;values.depthFactor[0]=(f&2)?.125f:-.0625f;data[7][1]=(f&4)?.5f:-.25f;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][0]=1;values.clipRegion[j][1]=1;values.clipRegion[j][2]=3;values.clipRegion[j][3]=3;}
   if(shape==0){data[4][0]=.125f;data[4][1]=.25f;data[5][0]=5.5f;data[5][1]=1.125f;data[6][0]=.375f;data[6][1]=6.25f;}
   if(shape==1){data[5][0]=1e30f;data[6][1]=1e30f;}
   if(shape==2){data[5][0]=0;data[5][1]=0;data[6][0]=0;data[6][1]=0;}
   if(shape==3){data[5][0]=INFINITY;data[6][1]=INFINITY;}
   if(shape==4){data[4][3]=0;data[5][3]=0;data[6][3]=0;}
   if(shape==5){data[4][3]=-.5f;data[5][3]=-1.25f;data[6][3]=-2;}
   if(shape==6){data[4][3]=.25f;data[5][3]=1.25f;data[6][3]=2.5f;values.clipRange[0][2]=100;values.clipRange[0][3]=250;}
   if(shape==7){data[4][0]=1e20f;data[4][1]=-1e20f;data[5][0]=2e20f;data[5][1]=1e20f;data[6][0]=-1e20f;data[6][1]=2e20f;}
  }
  if(test>=4096){
   unsigned f=(test-4096)%4096;const unsigned modes[]={PS_TEXTUREMODES_NONE,PS_TEXTUREMODES_PROJECT2D,PS_TEXTUREMODES_PASSTHRU,PS_TEXTUREMODES_CLIPPLANE};
   state.shader_stage_program=0;state.combiner_control=0;state.depth_clipping=false;state.window_clip_exclusive=false;state.z_perspective=false;
   for(unsigned j=0;j<4;j++){
    state.shader_stage_program|=modes[(f>>(j*2))&3]<<(j*5);state.dim_tex[j]=2;state.rect_tex[j]=!!(f&(1u<<(j+7)));state.alphakill[j]=!!(f&2048);
    for(unsigned k=0;k<4;k++)state.compare_mode[j][k]=!!(f&(1u<<(k+4)));
    values.texScale[j]=.5f+(j%3)*.5f;
    VkDescriptorImageInfo info={samplers[(f/256+j)%16],texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   state.combiner_control=((f/64)%9) | ((PS_COMBINERCOUNT_MUX_MSB|PS_COMBINERCOUNT_UNIQUE_C0|PS_COMBINERCOUNT_UNIQUE_C1)<<8);
   for(unsigned j=0;j<(state.combiner_control&255);j++){
    state.rgb_inputs[j]=encoded_inputs(f+j,false,false);state.alpha_inputs[j]=encoded_inputs(f+j+5,true,false);
    state.rgb_outputs[j]=(((f+j)&255)<<12)|(12<<8)|(4<<4)|5;state.alpha_outputs[j]=((f+j)&4)<<12|(13<<8)|(12<<4)|5;
   }
   unsigned reg=PS_REGISTER_T0+((f>>8)&3);if((f&1024) && (state.combiner_control&255))reg=PS_REGISTER_R0;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
   values.alphaKillMask[0]=(f/16)%16;values.alphaTestEnable[0]=f&1024;values.alphaFunc[0]=f%8;values.clipRange[0][2]=0;values.clipRange[0][3]=values.clipRange[0][1];values.depthOffset[0]=values.depthFactor[0]=0;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][0]=0;values.clipRegion[j][1]=0;values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;data[7][3]=(float)(f%32+1);if(test>=8192){state.point_sprite=true;state.rect_tex[3]=false;data[7][3]=-data[7][3];state.shader_stage_program=(state.shader_stage_program&~(31u<<15))|((f&1?PS_TEXTUREMODES_PROJECT2D:PS_TEXTUREMODES_PASSTHRU)<<15);state.final_inputs_0=0x200b0000;state.final_inputs_1=0x1b00;}values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;
  }
  if(test>=10240){
   unsigned f=test-10240;state.combiner_dynamic=true;
   if(test<14336){state.shader_stage_program=0;state.point_sprite=false;data[7][3]=0;}
   values.combCtl[0]=(f%9)|(((f&1 || f&8)?0x100:0)|(f&2?0x1000:0)|(f&4?0x10000:0));
   values.combFinal0[0]=dynamic_inputs(f+7,false,true);
   uint32_t ef=dynamic_inputs(f+11,false,true),g=dynamic_inputs(f+3,true,true);
   values.combFinal1[0]=(ef&0xffff0000u)|((g>>16)&0xff00u)|((f&7)<<5);
   if(f%17==0)values.combFinal0[0]=values.combFinal1[0]=0;
   for(unsigned j=0;j<8;j++){
    values.combRgbIn[j]=dynamic_inputs(f+j,false,true);values.combAlphaIn[j]=dynamic_inputs(f+j+5,true,!!(f&8));
    values.combRgbOut[j]=(((f+j)&255)<<12)|(((f+j*3)&15)<<8)|(((f+j)&15)<<4)|((f+j*5)&15);
    values.combAlphaOut[j]=((f+j)&4)<<12|((f&8?((f+j)&7):0)<<15)|(13<<8)|(12<<4)|((f+j)&15);
   }
   /* Keep the LSB float-to-uint conversion defined, including stage zero. */
   if(test>=14336)values.combCtl[0]|=0x100;
   if(test>=16384){
    static const float special[]={0.0f,-0.0f,0x1p-126f,0x1p-149f,1.00000011920928955078125f,-1.00000011920928955078125f,1e20f,-1e20f,INFINITY,-INFINITY,NAN,.1f,-.1f,.33333334f,-.75f,1.75f};
    state.shader_stage_program=0;state.point_sprite=false;data[7][3]=0;state.alpha_dynamic=true;values.alphaTestEnable[0]=0;values.combCtl[0]|=0x100;
    for(unsigned j=0;j<18;j++)for(unsigned k=0;k<4;k++)values.consts[j][k]=k==3?.5f:special[(f+j*3+k)%16];
    for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)data[j][k]=special[(f+j*5+k+7)%16];
   }
   state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;
   memset(state.rgb_inputs,0xa5,sizeof(state.rgb_inputs));memset(state.alpha_inputs,0x5a,sizeof(state.alpha_inputs));
  }
  if(test>=16896){
   unsigned f=test-16896,n=f,unit=n%4;n/=4;bool rect=n%2;n/=2;bool border=n%2;n/=2;unsigned key=n%4;n/=4;unsigned kernel=n%3;n/=3;unsigned alpha=n%4;n/=4;bool live=n%2;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.smooth_shading=!!(f&1);state.combiner_dynamic=live;state.alpha_dynamic=!!(alpha&1);state.depth_format=DEPTH_FORMAT_D24;
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D<<(unit*5);state.dim_tex[unit]=2;state.rect_tex[unit]=rect;state.border_adjust[unit]=border;state.colorkey_mode[unit]=key;
   state.conv_tex[unit]=kernel==0?CONVOLUTION_FILTER_DISABLED:kernel==1?CONVOLUTION_FILTER_GAUSSIAN:CONVOLUTION_FILTER_QUINCUNX;state.alphakill[unit]=!!(alpha&2);
   state.point_sprite=unit==3 && !rect && !!(f&128);data[7][3]=(f&1)?2:6;if(state.point_sprite)data[7][3]=-data[7][3];
   values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.alphaKillMask[0]=1u<<unit;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKey[j]=(f&32)?0x80200000u:0x00200000u;
    const uint32_t masks[]={0,0xff000000u,0x00ffffffu,0xffffffffu};values.colorKeyMask[j]=masks[(f/64+j)%4];
    for(unsigned k=0;k<3;k++){values.borderLogicalSize[j][k]=4.0f+j*.5f;values.borderInvRealSize[j][k]=1.0f/(12+j*2);}
    VkDescriptorImageInfo info={samplers[(f/16+j)%16],texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   uint32_t reg=PS_REGISTER_T0+unit;unsigned stages=(f/64)%9;
   state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;
    state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
   if(f>=1536){
    state.combiner_dynamic=!!(f&2);state.alpha_dynamic=!!(f&1);state.point_sprite=!!(f&4);data[7][3]=state.point_sprite?-2:2;
    state.shader_stage_program=0;state.combiner_control=2;values.combCtl[0]=2|0x100;
    for(unsigned j=0;j<4;j++){
     state.shader_stage_program|=PS_TEXTUREMODES_PROJECT2D<<(j*5);state.dim_tex[j]=2;state.rect_tex[j]=!!(f&(1u<<j)) && !(j==3 && state.point_sprite);state.border_adjust[j]=!!(f&(1u<<(j+4)));state.colorkey_mode[j]=(f+j)%4;state.conv_tex[j]=((f+j)%3)==0?CONVOLUTION_FILTER_DISABLED:((f+j)%3)==1?CONVOLUTION_FILTER_GAUSSIAN:CONVOLUTION_FILTER_QUINCUNX;
    }
    for(unsigned j=0;j<2;j++){
     state.rgb_inputs[j]=((8+j*2)<<24)|((9+j*2)<<16);state.rgb_outputs[j]=(12+j)<<4;state.alpha_inputs[j]=0x18200000;state.alpha_outputs[j]=12<<4;
     values.combRgbIn[j]=state.rgb_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
    }
    state.final_inputs_0=0x200c000d;state.final_inputs_1=0x1c00;
   }
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;
  }
  if(test>=18944){
   unsigned f=test-18944,unit=f%4,n=f/4,family=n%6;n/=6;bool live=n%2;n/=2;unsigned coordinate=n%64;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=!!(f&1);state.combiner_dynamic=live;state.alpha_dynamic=!!(f&256);
   fixture_family(&state,unit,family);state.rect_tex[unit]=state.dim_tex[unit]==2 && family!=4 && family!=5 && !!(f&64);state.border_adjust[unit]=!!(f&128);state.colorkey_mode[unit]=(f/256)%4;state.alphakill[unit]=!!(f&512);
   state.point_sprite=unit==3 && !state.rect_tex[3] && !!(f&1024);data[7][3]=(float)(128+coordinate);if(state.point_sprite)data[7][3]=-data[7][3];
   uint32_t reg=PS_REGISTER_T0+unit;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
   if(f>=3072){
    state.shader_stage_program=0;state.point_sprite=!!(f&4);data[7][3]=(float)(128+(f%128));if(state.point_sprite)data[7][3]=-data[7][3];
    for(unsigned j=0;j<4;j++){
     unsigned choice=(f&256)?(f/4)%6:(f/4+j*7)%6;fixture_family(&state,j,choice);state.rect_tex[j]=state.dim_tex[j]==2 && choice==2 && !!(f&(1u<<j)) && !(j==3 && state.point_sprite);state.border_adjust[j]=!!(f&(1u<<(j+4)));state.colorkey_mode[j]=(f&256)?COLOR_KEY_NONE:(int)((f+j)%4);if(f&256)state.alphakill[j]=false;
    }
    if(f&256)state.alpha_dynamic=false;
    state.combiner_control=2;values.combCtl[0]=2|0x100;
    for(unsigned j=0;j<2;j++){
     state.rgb_inputs[j]=((8+j*2)<<24)|((9+j*2)<<16);state.rgb_outputs[j]=(12+j)<<4;state.alpha_inputs[j]=0x18200000;state.alpha_outputs[j]=12<<4;
     values.combRgbIn[j]=state.rgb_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
    }
    state.final_inputs_0=0x200c000d;state.final_inputs_1=0x1c00;
   }
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    for(unsigned k=0;k<3;k++){values.borderLogicalSize[j][k]=4+j*.5f;values.borderInvRealSize[j][k]=1.0f/(12+j*2);}
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;
  }
  if(test>=23040){
   unsigned f=test-23040,unit=1+f%3,n=f/3,choice=n%4;n/=4;unsigned input=n%unit;n/=unit;bool bump=choice<2,volume_bump=bump && !!(n&1),rect=bump && !volume_bump && !!(n&2),live=!!(n&4);
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=!!(f&1);state.combiner_dynamic=live;state.alpha_dynamic=!!(f&256);
   const unsigned modes[]={PS_TEXTUREMODES_BUMPENVMAP,PS_TEXTUREMODES_BUMPENVMAP_LUM,PS_TEXTUREMODES_DPNDNT_AR,PS_TEXTUREMODES_DPNDNT_GB};
   unsigned producer=(f/32)%6;
   for(unsigned j=0;j<unit;j++){
    if(producer<4)fixture_family(&state,j,producer);
    else{state.shader_stage_program|=(producer==4?PS_TEXTUREMODES_PASSTHRU:PS_TEXTUREMODES_NONE)<<(j*5);state.dim_tex[j]=2;}
    if(producer<4 && (f&128))state.colorkey_mode[j]=COLOR_KEY_KILL_ALPHA;
    if(j>=1 && (f&1024)){
     unsigned chain=(j&1)?PS_TEXTUREMODES_DPNDNT_AR:PS_TEXTUREMODES_BUMPENVMAP_LUM;
     state.shader_stage_program=(state.shader_stage_program&~(31u<<(j*5)))|(chain<<(j*5));state.dim_tex[j]=2;state.tex_cubemap[j]=false;
     if(j>=2)state.other_stage_input|=(j-1)<<(16+(j-2)*4);
    }
   }
   state.shader_stage_program|=modes[choice]<<(unit*5);state.dim_tex[unit]=volume_bump?3:2;state.rect_tex[unit]=rect;state.border_adjust[unit]=bump && !!(f&64);state.colorkey_mode[unit]=(f/256)%4;state.alphakill[unit]=!!(f&512);
   if(unit>=2)state.other_stage_input|=input<<(16+(unit-2)*4);
   state.point_sprite=unit==3 && !rect && !!(f&2048);data[7][3]=(float)(128+f%64);if(state.point_sprite)data[7][3]=-data[7][3];
   values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.alphaKillMask[0]=(f/16)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKeyMask[j]=(f&64)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    values.bumpMat[j][0]=(j+1)*.0625f;values.bumpMat[j][1]=-(j+1)*.03125f;values.bumpMat[j][2]=.1f+(f&3)*.015625f;values.bumpMat[j][3]=.125f+(j%2)*.0625f;
    values.bumpScale[j]=.0625f;values.bumpOffset[j]=.75f;
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/64)%9,reg=PS_REGISTER_T0+unit;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   if(live){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;
  }
  if(test>=27136){
   unsigned f=test-27136,family=f%8,unit=family==0 || family==2 || family==4?2:3;
   const unsigned modes[]={PS_TEXTUREMODES_DOT_ST,PS_TEXTUREMODES_DOT_ST,PS_TEXTUREMODES_DOT_ZW,PS_TEXTUREMODES_DOT_ZW,PS_TEXTUREMODES_DOT_RFLCT_DIFF,PS_TEXTUREMODES_DOT_RFLCT_SPEC,PS_TEXTUREMODES_DOT_STR_3D,PS_TEXTUREMODES_DOT_STR_CUBE};
   unsigned mode=modes[family];bool cube_family=family==4 || family==5 || family==7,sampled=mode!=PS_TEXTUREMODES_DOT_ZW;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.smooth_shading=!!(f&1);state.depth_format=(f/8)%4;state.z_perspective=!!(f&32);state.depth_clipping=!!(f&1024);
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D | (PS_TEXTUREMODES_DOTPRODUCT<<5);state.dim_tex[0]=2;
   if(unit==3)state.shader_stage_program|=PS_TEXTUREMODES_DOTPRODUCT<<10;
   if(unit==3 && (f&128)){
    unsigned preceding=family==3?PS_TEXTUREMODES_DOT_ZW:PS_TEXTUREMODES_DOT_ST;
    state.shader_stage_program=(state.shader_stage_program&~(31u<<10))|(preceding<<10);state.dim_tex[2]=2;
    if(f&256)state.colorkey_mode[2]=COLOR_KEY_KILL_ALPHA;
   }
   state.shader_stage_program|=mode<<(unit*5);state.dim_tex[unit]=family==6 && (f&64)?3:2;state.tex_cubemap[unit]=cube_family && !!(f&64);
   state.point_sprite=!!(f&2048);state.rect_tex[unit]=sampled && !cube_family && state.dim_tex[unit]==2 && !!(f&128) && !(unit==3 && state.point_sprite);
   state.border_adjust[unit]=sampled && mode!=PS_TEXTUREMODES_DOT_ST && !!(f&256);state.colorkey_mode[unit]=sampled?(f/512)%4:0;state.alphakill[unit]=!!(f&16);state.alpha_dynamic=!!(f&512);
   state.other_stage_input=((f/8)%8) | (((f/64)%8)<<4) | (((f/512)%8)<<8) | (((f/16)%2)<<16) | (((f/32)%(family==4?2:3))<<20);
   if(f&128)state.colorkey_mode[0]=COLOR_KEY_KILL_ALPHA;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   if((family==2 || family==3) && (f&256)){values.clipRange[0][2]=.25f;values.clipRange[0][3]=1.5f;}
   values.depthFactor[0]=(f&16)?32:-16;values.depthOffset[0]=(f&8)?1000:-500;values.alphaKillMask[0]=(f/16)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.borderLogicalSize[j][0]=values.borderLogicalSize[j][1]=values.borderLogicalSize[j][2]=2;
    values.borderInvRealSize[j][0]=values.borderInvRealSize[j][1]=values.borderInvRealSize[j][2]=.125f;values.colorKeyMask[j]=(f&64)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/32)%9,reg=PS_REGISTER_T0+unit;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&512);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=.125f;data[7][3]=(float)(128+f%128);if(state.point_sprite)data[7][3]=-data[7][3];
  }
  if(test>=31232){
   unsigned f=test-31232,unit=f%4;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.shader_stage_program=((f&4)?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(unit*5);state.dim_tex[unit]=2;state.rect_tex[unit]=!!(f&8);state.shadow_map[unit]=(f&32)?19:1;state.tex_x8y24[unit]=!!(f&64);
   state.shadow_depth_func=(f&16)?SHADOW_DEPTH_FUNC_ALWAYS:SHADOW_DEPTH_FUNC_NEVER;state.colorkey_mode[unit]=(f/128)%4;state.alphakill[unit]=!!(f&256);state.alpha_dynamic=!!(f&512);state.smooth_shading=!!(f&1024);state.depth_format=DEPTH_FORMAT_D24;
   state.point_sprite=!!(f&2048) && !state.rect_tex[3];state.border_adjust[unit]=!!(f&32);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){values.texScale[j]=(f&32)?-1:1;values.colorKeyMask[j]=(f&64)?0xff000000u:0;values.colorKey[j]=0x80000000u;}
   unsigned stages=(f/16)%9,reg=PS_REGISTER_T0+unit;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;data[7][3]=state.point_sprite?-128:128;
  }
  if(test>=35328){
   unsigned f=test-35328,unit=f%3,consumer=unit+1,choice=(f/3)%4;const unsigned modes[]={PS_TEXTUREMODES_BUMPENVMAP,PS_TEXTUREMODES_BUMPENVMAP_LUM,PS_TEXTUREMODES_DPNDNT_AR,PS_TEXTUREMODES_DPNDNT_GB};
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=(f/8)%4;state.smooth_shading=!!(f&4);state.alpha_dynamic=!!(f&16);state.point_sprite=!!(f&512);
   state.shader_stage_program=(((f&4)?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(unit*5)) | (modes[choice]<<(consumer*5));state.dim_tex[unit]=2;state.shadow_map[unit]=(f&32)?19:1;state.shadow_depth_func=(f&64)?SHADOW_DEPTH_FUNC_ALWAYS:SHADOW_DEPTH_FUNC_NEVER;state.rect_tex[unit]=!!(f&8);state.tex_x8y24[unit]=!!(f&32);state.border_adjust[unit]=!!(f&256);
   state.colorkey_mode[unit]=(f/32)%4;state.colorkey_mode[consumer]=(f/128)%4;state.alphakill[unit]=!!(f&256);state.alphakill[consumer]=!!(f&64);
   bool volume_sample=choice<2 && !!(f&128);state.dim_tex[consumer]=volume_sample?3:2;state.rect_tex[consumer]=choice<2 && !volume_sample && !!(f&256) && !(consumer==3 && state.point_sprite);
   if(consumer>=2)state.other_stage_input=unit<<(16+(consumer-2)*4);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/8)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=j==unit && (f&32)?-1:1;values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    values.bumpMat[j][0]=.125f;values.bumpMat[j][1]=-.0625f;values.bumpMat[j][2]=.0625f;values.bumpMat[j][3]=.25f;values.bumpScale[j]=.125f;values.bumpOffset[j]=.75f;
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/64)%9,reg=PS_REGISTER_T0+consumer;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;data[7][3]=(float)(128+f%128);if(state.point_sprite)data[7][3]=-data[7][3];
  }
  if(test>=37376){
   unsigned f=test-37376;memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=!!(f&1);state.alpha_dynamic=!!(f&4);state.point_sprite=!!(f&128);
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D | (PS_TEXTUREMODES_DOTPRODUCT<<5) | (PS_TEXTUREMODES_DOT_RFLCT_DIFF<<10) | (((f&2)?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<15);
   state.dim_tex[0]=state.dim_tex[2]=state.dim_tex[3]=2;state.tex_cubemap[2]=!!(f&8);state.shadow_map[3]=(f&32)?19:1;state.shadow_depth_func=(f&16)?SHADOW_DEPTH_FUNC_ALWAYS:SHADOW_DEPTH_FUNC_NEVER;state.tex_x8y24[3]=!!(f&64);state.rect_tex[3]=!!(f&16) && !state.point_sprite;
   state.other_stage_input=(f%8) | (((f/8)%8)<<4) | (((f/64)%8)<<8) | (((f/16)%2)<<16) | (((f/32)%2)<<20);state.alphakill[3]=!!(f&32);values.alphaKillMask[0]=(f/8)%16;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=j==3 && (f&32)?-1:1;unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/16)%9,reg=PS_REGISTER_T0+2;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&256);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;data[7][3]=(float)(128+f%128);if(state.point_sprite)data[7][3]=-data[7][3];
  }
  if(test>=37888){
   unsigned f=test-37888,unit=f%4,format=(f/4)%4;bool packed=format==3;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.shader_stage_program=((f&32)?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(unit*5);state.dim_tex[unit]=2;state.shadow_map[unit]=1;state.shadow_depth_func=1+(f/16)%6;state.tex_x8y24[unit]=packed;state.rect_tex[unit]=!!(f&64);state.point_sprite=!!(f&2048) && !state.rect_tex[3];state.smooth_shading=!!(f&1);state.alpha_dynamic=!!(f&512);state.alphakill[unit]=!!(f&256);state.colorkey_mode[unit]=(f/128)%4;state.depth_format=(f/64)%4;state.border_adjust[unit]=!!(f&128);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    unsigned sampler=(f/16+j)%16;if(j==unit && packed)sampler&=~3u;
    VkDescriptorImageInfo info={samplers[sampler],j==unit?(packed?packed_depth.view:depth_views[format]):texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/64)%9,reg=PS_REGISTER_T0+unit;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float maximum=format==0?65535:format==2?511.9375f:16777215;
   const float refs[]={0,.125f,.25f,.5f,.75f,1,1.5f,-.25f};float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));for(unsigned j=0;j<4;j++)data[0][j]=maximum;data[7][0]=.5f;data[7][1]=refs[(f/97)%8];data[7][2]=(f/7)&1?1:-1;data[7][3]=(float)(256+(f/17)%8);if(state.point_sprite)data[7][3]=-data[7][3];
  }
  if(test>=41984){
   unsigned f=test-41984,packed_unit=f%4,float_unit=(packed_unit+1)%4,ordinary=(packed_unit+2)%4,cube_unit=(packed_unit+3)%4,format=(f/4)%3;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=(f/32)%4;state.smooth_shading=!!(f&1);state.alpha_dynamic=!!(f&128);state.point_sprite=!!(f&512);state.shadow_depth_func=1+(f/16)%6;
   for(unsigned j=0;j<4;j++){
    unsigned mode=(j==packed_unit || j==float_unit)?PS_TEXTUREMODES_PROJECT3D:j==cube_unit?PS_TEXTUREMODES_CUBEMAP:(f&32)?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D;
    state.shader_stage_program|=mode<<(j*5);state.dim_tex[j]=j==ordinary && (f&32)?3:2;state.tex_cubemap[j]=j==cube_unit;state.shadow_map[j]=j==packed_unit || j==float_unit;state.tex_x8y24[j]=j==packed_unit;
    state.rect_tex[j]=(j==packed_unit || j==float_unit) && !!(f&64) && !(j==3 && state.point_sprite);values.texScale[j]=.5f+(j%3)*.5f;
    unsigned sampler=(f/16+j)%16;if(j==packed_unit)sampler&=~3u;
    VkImageView view=j==packed_unit?packed_depth.view:j==float_unit?depth_views[format]:j==cube_unit?cube.view:state.dim_tex[j]==3?volume.view:texture.view;
    VkDescriptorImageInfo info={samplers[sampler],view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
    data[0][j]=j==packed_unit?16777215:j==float_unit?(format==0?65535:format==1?16777215:511.9375f):1;
   }
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/8)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   state.final_inputs_0=((PS_REGISTER_T0+packed_unit)<<24)|((PS_REGISTER_T0+float_unit)<<16)|((PS_REGISTER_T0+cube_unit)<<8)|(PS_REGISTER_T0+ordinary);state.final_inputs_1=(PS_REGISTER_T0+ordinary+16)<<8;
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   const float refs[]={0,.125f,.25f,.5f,.75f,1,1.5f,-.25f};float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=refs[(f/97)%8];data[7][2]=(f/7)&1?1:-1;data[7][3]=(float)(256+(f/17)%8);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=44032){
   unsigned f=test-44032,unit=f%4;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D<<(unit*5);state.dim_tex[unit]=2;
   state.depth_format=((f/32)%4)|DEPTH_FORMAT_UI_CLAMP;state.z_perspective=!!(f&16);state.depth_clipping=!!(f&64);state.smooth_shading=!!(f&128);
   state.rect_tex[unit]=!!(f&256);state.point_sprite=!!(f&2048) && !(unit==3 && state.rect_tex[unit]);state.alpha_dynamic=!!(f&512);state.alphakill[unit]=!!(f&8);state.colorkey_mode[unit]=(f/64)%4;state.border_adjust[unit]=!!(f&1024);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/8)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.borderInvRealSize[j][0]=values.borderInvRealSize[j][1]=values.borderInvRealSize[j][2]=.25f;values.borderLogicalSize[j][0]=values.borderLogicalSize[j][1]=values.borderLogicalSize[j][2]=2;
    values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    VkDescriptorImageInfo info={samplers[(f/16+j)%16],texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/16)%9,reg=PS_REGISTER_T0+unit;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};
   if(f&4){plane[1][0]=.125f;plane[1][1]=.25f;plane[2][0]=.625f;plane[2][1]=.75f;}
   memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=(f&4)?-0.0f:0.0f;data[7][2]=1;data[7][3]=(float)(1+(f/8)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=48128){
   unsigned f=test-48128,unit=f%4,flags=3+4*((f/4)%8);bool encoded=!!(f&64);
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));
   state.shader_stage_program=((f&32)?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(unit*5);state.dim_tex[unit]=2;state.shadow_map[unit]=flags;state.shadow_depth_func=1+(f/128)%6;
   state.depth_format=(f/512)%4;state.smooth_shading=!!(f&256);state.alpha_dynamic=!!(f&512);state.alphakill[unit]=!!(f&1024);state.rect_tex[unit]=!!(f&128);state.point_sprite=!!(f&2048) && !(unit==3 && state.rect_tex[unit]);state.colorkey_mode[unit]=(f/256)%4;state.border_adjust[unit]=!!(f&1024);state.conv_tex[unit]=99;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/16)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=(encoded?-1:1)*(.5f+(j%3)*.5f);values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    unsigned sampler=(f/16+j)%16;if(j==unit && encoded)sampler&=~3u;
    VkDescriptorImageInfo info={samplers[sampler],j==unit && encoded?encoded_f16.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned stages=(f/64)%9,reg=PS_REGISTER_T0+unit;state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   const float refs[]={0,.0001f,.125f,.5f,.75f,1,1.5f,-.25f};float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));for(unsigned j=0;j<4;j++)data[0][j]=511.9375f;data[7][0]=.5f;data[7][1]=refs[(f/97)%8];data[7][2]=(f/7)&1?1:-1;data[7][3]=(float)(256+(f/17)%8);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=52224){
   unsigned f=test-52224,a=f%4,b=(a+1)%4,packed=(a+2)%4,other=(a+3)%4;bool volume_sample=!!(f&32),ui=!!(f&128),rect_marker=!!(f&256);
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=((f/32)%4)|(ui?DEPTH_FORMAT_UI_CLAMP:0);state.z_perspective=!!(f&1024);state.smooth_shading=!!(f&1);state.point_sprite=!!(f&512);state.alpha_dynamic=!!(f&64);state.shadow_depth_func=1+(f/8)%6;
   for(unsigned j=0;j<4;j++){
    unsigned mode=j==a?((f&4)?PS_TEXTUREMODES_PROJECT2D:PS_TEXTUREMODES_PROJECT3D):j==b?PS_TEXTUREMODES_PROJECT2D:j==packed?PS_TEXTUREMODES_PROJECT3D:volume_sample?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_CUBEMAP;
    state.shader_stage_program|=mode<<(j*5);state.dim_tex[j]=j==other && volume_sample?3:2;state.tex_cubemap[j]=j==other && !volume_sample;
    state.shadow_map[j]=j==a?3+4*((f/4)%8):j==b?3+4*((f/17+3)%8):j==packed?1:0;state.tex_x8y24[j]=j==packed;
    state.rect_tex[j]=(j==a || j==b) && !!(f&(1u<<(j+4))) && !(j==3 && state.point_sprite);state.alphakill[j]=j!=other && !!(f&(1u<<(j+6)));state.colorkey_mode[j]=(f/128+j)%4;
    values.texScale[j]=j==a?-.5f:j==b?1.5f:1;values.colorKeyMask[j]=(f&64)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    unsigned sampler=(f/16+j)%16;if(j==a || j==packed)sampler&=~3u;
    VkImageView view=j==a?encoded_f16.view:j==b?texture.view:j==packed?packed_depth.view:volume_sample?volume.view:cube.view;
    VkDescriptorImageInfo info={samplers[sampler],view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
    data[0][j]=j==packed?16777215:j==other?1:511.9375f;
   }
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/8)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   state.final_inputs_0=((PS_REGISTER_T0+a)<<24)|((PS_REGISTER_T0+b)<<16)|((PS_REGISTER_T0+packed)<<8)|(PS_REGISTER_T0+other);state.final_inputs_1=(PS_REGISTER_T0+other+16)<<8;
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   const float refs[]={0,.0001f,.125f,.5f,.75f,1,1.5f,-.25f};float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};
   if(ui && rect_marker){plane[1][0]=.125f;plane[1][1]=.25f;plane[2][0]=.625f;plane[2][1]=.75f;}
   memcpy(data[4],plane,sizeof(plane));data[1][0]=rect_marker?-0.0f:0.0f;data[7][0]=.5f;data[7][1]=refs[(f/97)%8];data[7][2]=(f/7)&1?1:-1;data[7][3]=(float)(256+(f/17)%8);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=54272){
   unsigned f=test-54272,unit=f%4,signs=(1+(f/4)%15)|(((f/60)%16)<<4);
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D<<(unit*5);state.dim_tex[unit]=2;state.g8b8_signed[unit]=signs;
   state.g8b8_mip_flags=((f&128)?1u<<unit:0)|((f&256)?1u<<(unit+4):0);state.g8b8_mip_clamp_u=(f&32)?1u<<unit:0;state.g8b8_mip_clamp_v=(f&64)?1u<<unit:0;state.g8b8_mip_mirror=(f&512)?(1u<<unit)|(1u<<(unit+4)):0;
   if(f&2048)psh_g8b8_set_extended_lod(&state,unit,4+(f/128)%5,1+(f/16)%3,1+(f/48)%3);
   state.depth_format=(f/128)%4;state.smooth_shading=!!(f&1024);state.rect_tex[unit]=!!(f&1024);state.point_sprite=!!(f&512) && !(unit==3 && state.rect_tex[unit]);state.alpha_dynamic=!!(f&256);state.colorkey_mode[unit]=(f&1024)?(f/512)%4:COLOR_KEY_NONE;state.alphakill[unit]=!!(f&1024);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.consts[0][j]=values.consts[16][j]=.5f;values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    VkDescriptorImageInfo info={samplers[(f/16+j)%16],j==unit?signed_gb.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned reg=PS_REGISTER_T0+unit;state.final_inputs_0=(PS_REGISTER_C0<<24)|((reg|0xc0u)<<16)|(0x20u<<8);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=0;data[7][2]=1;data[7][3]=(float)(1+(f/17)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=58368){
   unsigned f=test-58368,input=f%2;bool ui=!!(f&128),marked=!!(f&256),volume_sample=!!(f&64);
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=((f/32)%4)|(ui?DEPTH_FORMAT_UI_CLAMP:0);state.z_perspective=!!(f&32);state.smooth_shading=!!(f&1);state.point_sprite=!!(f&512);
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D|(PS_TEXTUREMODES_PROJECT2D<<5)|(((f&4)?PS_TEXTUREMODES_BUMPENVMAP_LUM:PS_TEXTUREMODES_BUMPENVMAP)<<10)|((volume_sample?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_CUBEMAP)<<15);
   state.other_stage_input=input<<16;state.dim_tex[0]=state.dim_tex[1]=2;state.dim_tex[2]=(f&16)?3:2;state.dim_tex[3]=volume_sample?3:2;state.tex_cubemap[3]=!volume_sample;
   for(unsigned j=0;j<2;j++){
    unsigned signs=(1+(f/2+j*7)%15)|(((f/30+j*3)%16)<<4);state.g8b8_signed[j]=signs;unsigned bit=1u<<j;
    state.g8b8_mip_flags|=((f&(8u<<j))?bit:0)|((f&(32u<<j))?bit<<4:0);state.g8b8_mip_clamp_u|=(f&64)?bit:0;state.g8b8_mip_clamp_v|=(f&128)?bit:0;state.g8b8_mip_mirror|=(f&256)?bit:0;
    if(f&1024)psh_g8b8_set_extended_lod(&state,j,4+(f/128+j)%5,1+(f/16+j)%3,1+(f/48+j)%3);
    state.rect_tex[j]=!!(f&(16u<<j));
   }
   state.rect_tex[2]=state.dim_tex[2]==2 && !!(f&128);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.consts[0][j]=values.consts[16][j]=.5f;
    values.bumpMat[j][0]=.125f;values.bumpMat[j][1]=-.0625f;values.bumpMat[j][2]=.0625f;values.bumpMat[j][3]=.25f;values.bumpScale[j]=.125f;values.bumpOffset[j]=.75f;
    VkImageView view=j<2?signed_gb.view:j==2?(state.dim_tex[2]==3?volume.view:texture.view):volume_sample?volume.view:cube.view;
    VkDescriptorImageInfo info={samplers[(f/16+j)%16],view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   state.combiner_control=1;state.rgb_inputs[0]=((PS_REGISTER_T0|0xc0u)<<24)|(PS_REGISTER_C0<<16)|((PS_REGISTER_T0+1+0xc0u)<<8)|PS_REGISTER_C0;state.rgb_outputs[0]=PS_REGISTER_R0<<8;
   state.final_inputs_0=(PS_REGISTER_C0<<24)|((PS_REGISTER_R0|0xc0u)<<16)|((PS_REGISTER_T0+2)<<8)|PS_REGISTER_C0;state.final_inputs_1=(PS_REGISTER_T0+3+16)<<8;
   values.combCtl[0]=1;values.combRgbIn[0]=state.rgb_inputs[0];values.combRgbOut[0]=state.rgb_outputs[0];values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};if(ui && marked){plane[1][0]=.125f;plane[1][1]=.25f;plane[2][0]=.625f;plane[2][1]=.75f;}
   memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=marked?-0.0f:0.0f;data[7][2]=1;data[7][3]=(float)(1+(f/17)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=60416){
   unsigned f=test-60416,unit=f%4,shape=(f/4)%3,conv=shape==2?0:(f/12)%3;bool volume_ignored=shape==2,project3=shape==1;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.shader_stage_program=(project3?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(unit*5);state.dim_tex[unit]=volume_ignored || (project3 && (f&32))?3:2;state.tex_x8y24[unit]=true;
   state.conv_tex[unit]=conv==1?CONVOLUTION_FILTER_GAUSSIAN:conv==2?CONVOLUTION_FILTER_QUINCUNX:CONVOLUTION_FILTER_DISABLED;state.tex_cubemap[unit]=!!(f&64) && (volume_ignored || project3 || conv);
   state.depth_format=(f/128)%4;state.smooth_shading=!!(f&1024);state.rect_tex[unit]=!volume_ignored && !!(f&128);state.point_sprite=!!(f&512) && !(unit==3 && state.rect_tex[unit]);state.alpha_dynamic=!!(f&256);state.colorkey_mode[unit]=(f&2048)?(f/512)%4:COLOR_KEY_NONE;state.alphakill[unit]=!!(f&1024);state.border_adjust[unit]=!!(f&256);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.consts[16][j]=volume_ignored?1.0f:0x1p-33f;values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    values.borderLogicalSize[j][0]=values.borderLogicalSize[j][1]=values.borderLogicalSize[j][2]=2;values.borderInvRealSize[j][0]=values.borderInvRealSize[j][1]=values.borderInvRealSize[j][2]=.125f;
    unsigned sampler=(f/16+j)%16;if(j==unit && !volume_ignored)sampler&=~3u;
    VkImageView view=j==unit?(volume_ignored?volume.view:packed_depth.view):texture.view;
    VkDescriptorImageInfo info={samplers[sampler],view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned reg=PS_REGISTER_T0+unit;state.final_inputs_0=(PS_REGISTER_C0<<24)|((reg|0xc0u)<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=0;data[7][2]=1;data[7][3]=(float)(1+(f/17)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=64512){
   unsigned f=test-64512,raw=f%2,gb=raw^1,other=(f/4)%4,conv=(f/32)%3;bool project3=!!(f&4),ui=!!(f&128),marked=!!(f&256);
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=((f/32)%4)|(ui?DEPTH_FORMAT_UI_CLAMP:0);state.z_perspective=!!(f&64);state.smooth_shading=!!(f&2);state.point_sprite=!!(f&512);state.shadow_depth_func=1+(f/16)%6;
   state.shader_stage_program=((project3?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(raw*5))|(PS_TEXTUREMODES_PROJECT2D<<(gb*5))|(((f&8)?PS_TEXTUREMODES_BUMPENVMAP_LUM:PS_TEXTUREMODES_BUMPENVMAP)<<10)|((other==0?PS_TEXTUREMODES_PROJECT3D:other==1?PS_TEXTUREMODES_CUBEMAP:PS_TEXTUREMODES_PROJECT3D)<<15);
   state.dim_tex[raw]=project3 && (f&8)?3:2;state.dim_tex[gb]=2;state.dim_tex[2]=(f&16)?3:2;state.dim_tex[3]=other==0?3:2;state.tex_cubemap[3]=other==1;state.tex_x8y24[raw]=true;
   state.conv_tex[raw]=project3 || !conv?CONVOLUTION_FILTER_DISABLED:conv==1?CONVOLUTION_FILTER_GAUSSIAN:CONVOLUTION_FILTER_QUINCUNX;state.tex_cubemap[raw]=!project3 && conv && !!(f&64);
   state.g8b8_signed[gb]=(1+(f/2)%15)|(((f/30)%16)<<4);unsigned bit=1u<<gb;state.g8b8_mip_flags=((f&16)?bit:0)|((f&32)?bit<<4:0);state.g8b8_mip_clamp_u=(f&64)?bit:0;state.g8b8_mip_clamp_v=(f&128)?bit:0;state.g8b8_mip_mirror=(f&256)?bit:0;if(f&1024)psh_g8b8_set_extended_lod(&state,gb,4+(f/128)%5,1+(f/16)%3,1+(f/48)%3);
   unsigned input=(f&8)?gb:(f/4)%2;state.other_stage_input=input<<16;state.rect_tex[raw]=!!(f&16);state.rect_tex[gb]=!!(f&32);state.rect_tex[2]=state.dim_tex[2]==2 && !!(f&128);
   if(other==2){state.shadow_map[3]=1;state.tex_x8y24[3]=true;}if(other==3)state.shadow_map[3]=3+4*((f/16)%8);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=j==3 && other==3?-1:.5f+(j%3)*.5f;values.consts[0][j]=0x1p-33f;values.consts[1][j]=.25f;values.consts[16][j]=.5f;values.consts[17][j]=.125f;
    values.bumpMat[j][0]=.125f;values.bumpMat[j][1]=-.0625f;values.bumpMat[j][2]=.0625f;values.bumpMat[j][3]=.25f;values.bumpScale[j]=.125f;values.bumpOffset[j]=.75f;
    VkImageView view=j==raw?packed_depth.view:j==gb?signed_gb.view:j==2?(state.dim_tex[2]==3?volume.view:texture.view):other==0?volume.view:other==1?cube.view:other==2?packed_depth.view:encoded_f16.view;
    unsigned sampler=(f/16+j)%16;if(j==raw || (j==3 && other>=2))sampler&=~3u;
    VkDescriptorImageInfo info={samplers[sampler],view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   state.combiner_control=1|(PS_COMBINERCOUNT_MUX_MSB<<8);state.rgb_inputs[0]=((PS_REGISTER_T0+raw+0xc0u)<<24)|(PS_REGISTER_C0<<16)|((PS_REGISTER_T0+gb+0xc0u)<<8)|PS_REGISTER_C1;state.rgb_outputs[0]=(PS_REGISTER_R0<<8)|((f&128)?PS_COMBINEROUTPUT_AB_CD_MUX<<12:0);
   state.final_inputs_0=(PS_REGISTER_C0<<24)|((PS_REGISTER_R0|0xc0u)<<16)|((PS_REGISTER_T0+2)<<8)|PS_REGISTER_C1;state.final_inputs_1=(PS_REGISTER_T0+3+16)<<8;
   values.combCtl[0]=state.combiner_control;values.combRgbIn[0]=state.rgb_inputs[0];values.combRgbOut[0]=state.rgb_outputs[0];values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};if(ui && marked){plane[1][0]=.125f;plane[1][1]=.25f;plane[2][0]=.625f;plane[2][1]=.75f;}
   memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=marked?-0.0f:0.0f;data[7][2]=1;data[7][3]=(float)(1+(f/17)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=66560){
   unsigned f=test-66560,family=f%8,unit=(f/8)%4,reg=PS_REGISTER_V0;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.smooth_shading=!!(f&16);state.depth_format=(f/32)%4;state.z_perspective=!!(f&128);state.depth_clipping=!!(f&256);state.alpha_dynamic=!!(f&512);state.point_sprite=!!(f&2048);
   for(unsigned j=0;j<4;j++)state.dim_tex[j]=2;
   if(family==1){state.shader_stage_program=PS_TEXTUREMODES_PASSTHRU<<(unit*5);reg=PS_REGISTER_T0+unit;}
   if(family==2)state.shader_stage_program=PS_TEXTUREMODES_CLIPPLANE<<(unit*5);
   if(family==3){state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D<<(unit*5);reg=PS_REGISTER_T0+unit;}
   if(family==4){state.shader_stage_program=PS_TEXTUREMODES_PROJECT3D<<(unit*5);state.dim_tex[unit]=(f&64)?3:2;reg=PS_REGISTER_T0+unit;}
   if(family==5){state.shader_stage_program=PS_TEXTUREMODES_CUBEMAP<<(unit*5);state.tex_cubemap[unit]=!!(f&64);reg=PS_REGISTER_T0+unit;}
   if(family==6){state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D|(((f&64)?PS_TEXTUREMODES_BUMPENVMAP_LUM:PS_TEXTUREMODES_BUMPENVMAP)<<5);state.dim_tex[1]=(f&128)?3:2;reg=PS_REGISTER_T0+1;}
   if(family==7)state.shader_stage_program=PS_TEXTUREMODES_PASSTHRU|(PS_TEXTUREMODES_DOTPRODUCT<<5)|(PS_TEXTUREMODES_DOT_ZW<<10);
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.bumpMat[j][0]=.125f;values.bumpMat[j][1]=-.0625f;values.bumpMat[j][2]=.0625f;values.bumpMat[j][3]=.25f;values.bumpScale[j]=.125f;values.bumpOffset[j]=.75f;
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   fixture_dead_meta(&state,f);
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=0;data[7][2]=1;data[7][3]=(float)(1+(f/17)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }

  if(test>=70656){
   unsigned f=test-70656,map=(f/2)%8;bool packed=!(f&1),passthrough=!!(f&64) && map!=4;const unsigned flags[]={0,1,3,7,15,31,64,255};
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));memset(data,0,sizeof(data));
   state.shader_stage_program=(passthrough?PS_TEXTUREMODES_PASSTHRU:PS_TEXTUREMODES_PROJECT2D)|(PS_TEXTUREMODES_DOTPRODUCT<<5)|(PS_TEXTUREMODES_DOTPRODUCT<<10)|(PS_TEXTUREMODES_DOT_STR_3D<<15);
   for(unsigned j=0;j<4;j++)state.dim_tex[j]=2;
   state.other_stage_input=map|(map<<4)|(map<<8);state.tex_x8y24[3]=packed;state.shadow_map[3]=flags[(f/16)%8];state.shadow_depth_func=(f/64)%8;state.conv_tex[3]=(f&64)?99:CONVOLUTION_FILTER_GAUSSIAN;state.tex_cubemap[3]=!!(f&32);
   state.rect_tex[3]=!!(f&128);state.border_adjust[3]=!!(f&256);state.point_sprite=!!(f&512)&&!state.rect_tex[3];state.smooth_shading=!!(f&16);state.depth_format=(f/512)%4;
   state.alpha_dynamic=!!(f&512);state.alpha_test=!!(f&1024);state.alphakill[0]=!!(f&256);state.alphakill[3]=!!(f&1024);state.colorkey_mode[3]=(f&2048)?(f/256)%4:COLOR_KEY_NONE;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.alphaKillMask[0]=(f/16)%16;values.alphaTestEnable[0]=!!(f&32);values.alphaFunc[0]=(f/32)%8;values.alphaRef[0]=128;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=(.5f+(j%3)*.5f)*((f&64)?-1:1);values.consts[0][j]=values.consts[16][j]=packed?0x1p-33f:1;values.colorKey[j]=0xff0000ffu;values.colorKeyMask[j]=(f&64)?0xff000000u:0xffffffffu;
    values.borderLogicalSize[j][0]=values.borderLogicalSize[j][1]=values.borderLogicalSize[j][2]=2;values.borderInvRealSize[j][0]=values.borderInvRealSize[j][1]=values.borderInvRealSize[j][2]=.125f;
    unsigned sampler=(f/16+j)%16;if(j==3 && packed)sampler&=~3u;
    VkDescriptorImageInfo info={samplers[sampler],j==3 && packed?packed_depth.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   unsigned reg=PS_REGISTER_T0+3;state.final_inputs_0=(PS_REGISTER_C0<<24)|((reg|0xc0u)<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   state.combiner_dynamic=!!(f&1024);if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][2]=1;data[7][3]=(float)(1+(f/17)%64);if(state.point_sprite)data[7][3]=-data[7][3];
  }
  if(test>=74752){
   unsigned i=test-74752;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));memset(data,0,sizeof(data));
   unsigned family=i%9,unit=family==2||family==4?2:family==5||family==6?3:family==3?0:1;
  const unsigned modes[]={PS_TEXTUREMODES_BUMPENVMAP,PS_TEXTUREMODES_BUMPENVMAP_LUM,PS_TEXTUREMODES_DOT_ST,PS_TEXTUREMODES_CUBEMAP,PS_TEXTUREMODES_DOT_RFLCT_DIFF,PS_TEXTUREMODES_DOT_RFLCT_SPEC,PS_TEXTUREMODES_DOT_STR_CUBE,PS_TEXTUREMODES_DPNDNT_AR,PS_TEXTUREMODES_DPNDNT_GB};
  for(unsigned j=0;j<4;j++)state.dim_tex[j]=2;
  state.shader_stage_program=modes[family]<<(unit*5);if(unit)state.shader_stage_program|=PS_TEXTUREMODES_PASSTHRU;
  if(family==2||family==4||family==5||family==6)state.shader_stage_program|=PS_TEXTUREMODES_DOTPRODUCT<<5;
  if(family==5||family==6)state.shader_stage_program|=PS_TEXTUREMODES_DOTPRODUCT<<10;
  state.tex_cubemap[unit]=(family==3||family==4||family==5||family==6)&&!!(i&16);
  state.dim_tex[unit]=(family<2 && (i&32))?3:2;
  state.rect_tex[unit]=(family<2||family==3||family==5||family==6) && state.dim_tex[unit]==2 && !!(i&64);
  state.border_adjust[unit]=(family<2||family==3||family==4||family==5||family==6)&&!!(i&128);
  unsigned reg=PS_REGISTER_T0+unit;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
  state.smooth_shading=!!(i&256);state.combiner_dynamic=!!(i&512);state.alpha_dynamic=!!(i&1024);state.depth_format=DEPTH_FORMAT_D24;
   state.tex_x8y24[unit]=true;
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.bumpMat[j][0]=.0625f;values.bumpMat[j][3]=.125f;values.bumpScale[j]=.0625f;values.bumpOffset[j]=.75f;
    for(unsigned k=0;k<3;k++){values.borderLogicalSize[j][k]=2;values.borderInvRealSize[j][k]=.125f;}
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(i/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][2]=1;data[7][3]=(float)(1+(i/17)%64);
  }
  if(test>=76288){
   unsigned f=test-76288,unit=(f/640)%4;const unsigned flags[]={1,3,7,15,31};
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));memset(data,0,sizeof(data));
   state.depth_format=DEPTH_FORMAT_D24;state.shader_stage_program=PS_TEXTUREMODES_PROJECT3D<<(unit*5);state.dim_tex[unit]=1+(f/40)%4;state.tex_cubemap[unit]=!!((f/160)%2);state.shadow_map[unit]=flags[f%5];state.shadow_depth_func=(f/5)%8;state.rect_tex[unit]=!!((f/320)%2);
   state.combiner_dynamic=!!(f&128);state.alpha_dynamic=!!(f&256);state.smooth_shading=!!(f&64);
   unsigned reg=PS_REGISTER_T0+unit;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=(f&64)?-1:1;unsigned sampler=(f/16+j)%16;
    VkDescriptorImageInfo info={samplers[sampler],state.shadow_map[j]&2?encoded_f16.view:depth_views[(f/80)%3],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
    data[0][j]=(state.shadow_map[j]&2)?511.9375f:(f/80)%3==0?65535:16777215;
   }
   const float refs[]={0,.0001f,.125f,.5f,.75f,1,1.5f,-.25f};float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=refs[(f/97)%8];data[7][2]=(f/7)&1?1:-1;data[7][3]=(float)(256+(f/17)%8);
  }
  if(test>=77824){
   unsigned i=test-77824;memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));memset(data,0,sizeof(data));
   unsigned family=i%3,unit=(i/3)%4,n=i/12,reg=PS_REGISTER_T0+unit;
  state.depth_format=DEPTH_FORMAT_D24;state.shader_stage_program=(family==1?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(unit*5);state.dim_tex[unit]=2;
  state.tex_x8y24[unit]=family!=2 || !!(n&4);state.shadow_map[unit]=family==1?0:family==0?1:1+n%31;state.shadow_depth_func=family==2?(n&1?SHADOW_DEPTH_FUNC_ALWAYS:SHADOW_DEPTH_FUNC_NEVER):1+n%6;
  state.rect_tex[unit]=family!=2 && !!(n&2);state.smooth_shading=!!(n&8);state.combiner_dynamic=!!(n&16);state.alpha_dynamic=!!(n&32);
  state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;state.dim_tex[unit]=family==0?2:family==1?1+n%4:3;state.tex_cubemap[unit]=!!(n&1);
   if(family==1)state.final_inputs_0=(PS_REGISTER_C0<<24)|((reg|0xc0u)<<16);
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;if(state.combiner_dynamic)state.final_inputs_0=state.final_inputs_1=0;
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.consts[0][j]=values.consts[16][j]=0x1p-33f;data[0][j]=16777215;
    VkDescriptorImageInfo info={samplers[((n/4+j)%16)&~3u],packed_depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   const float refs[]={0,.125f,.5f,.75f,1,1.5f,-.25f,.0001f};float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][0]=.5f;data[7][1]=refs[n%8];data[7][2]=(n&4)?1:-1;data[7][3]=(float)(256+n%8);
  }
  if(test>=79360 && test<80656){
   unsigned choice=test-79360;composition_state(&state,choice,choice%24);
   memset(&values,0,sizeof(values));memset(data,0,sizeof(data));
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;
   values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    unsigned mode=(state.shader_stage_program>>(j*5))&31;
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKey[j]=0x80200000u+(j<<8);const uint32_t masks[]={0,0xff000000u,0x00ffffffu,0xffffffffu};values.colorKeyMask[j]=masks[(choice/24+j)%4];
    VkImageView view=mode==PS_TEXTUREMODES_CUBEMAP?cube.view:mode==PS_TEXTUREMODES_PROJECT3D?volume.view:texture.view;
    VkDescriptorImageInfo info={samplers[(choice/16+j)%16],view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));
   data[7][3]=state.point_sprite?-1:1;data[7][2]=1;
  }
  if(test>=16896 && test<18944){
   PshState current=state; /* bounded test-only metadata witness */
   for(unsigned j=0;j<4;j++){
    if(state.conv_tex[j]==CONVOLUTION_FILTER_QUINCUNX)state.conv_tex[j]=(enum ConvolutionFilter)1;
    else if(state.conv_tex[j]==CONVOLUTION_FILTER_GAUSSIAN)state.conv_tex[j]=(enum ConvolutionFilter)2;
   }
   pgraph_glsl_psh_migrate_seed(&state);
   assert(!memcmp(&current,&state,sizeof(state)));
  }
  if(test>=80656){
   unsigned f=test-80656;alias_state(&state,f);memset(&values,0,sizeof(values));memset(data,0,sizeof(data));
   values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=values.clipRange[0][3]=16777215;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<18;j++)for(unsigned k=0;k<4;k++)values.consts[j][k]=.1f+((j+k+f)%7)*.1f;
   values.fogColor[0][0]=.1f;values.fogColor[0][1]=.3f;values.fogColor[0][2]=.7f;
   const float colors[4][4]={{.1f,.3f,.7f,.6f},{.9f,.2f,.4f,.8f},{.3f,.6f,.1f,.7f},{.5f,.8f,.2f,.4f}};memcpy(data,colors,sizeof(colors));
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][2]=1;
  }
  const PshState *native_state=&state;
#ifdef COMPOSITION_DROP_T3
  /* Bounded test-only metadata mutation; prove the GPU comparison notices
   * the lost contribution while the reference keeps all four stages. */
  PshState mutation;
  if(test>=79360 && test<80656){mutation=state;mutation.rgb_inputs[0]&=~255u;mutation.alpha_inputs[0]&=~255u;native_state=&mutation;}
#endif
  GlslcResult modules[2];SpvPshOptions opts={.spirv_1_6=!!(test&256),.ubo_binding=1,.tex_binding=2};assert(pgraph_spv_psh(native_state,opts,&modules[1]));MString *source=pgraph_glsl_gen_psh(&state,(GenPshGlslOptions){.vulkan=true,.locations=true,.ubo_binding=1,.tex_binding=2});GlslcOptions go={.spirv_1_6=opts.spirv_1_6,.emit_layout=true};if(!glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(source),&go,&modules[0])){fprintf(stderr,"fragment %u %d %s\n",test,modules[0].error_line,modules[0].error);abort();}if(test>=74752 && test<76288){PshState canonical=state;for(unsigned j=0;j<4;j++)canonical.tex_x8y24[j]=false;MString *other=pgraph_glsl_gen_psh(&canonical,(GenPshGlslOptions){.vulkan=true,.locations=true,.ubo_binding=1,.tex_binding=2});assert(!strcmp(mstring_get_str(source),mstring_get_str(other)));mstring_unref(other);}if(test>=76288 && test<77824){PshState canonical=state;for(unsigned j=0;j<4;j++){canonical.dim_tex[j]=2;canonical.tex_cubemap[j]=false;}MString *other=pgraph_glsl_gen_psh(&canonical,(GenPshGlslOptions){.vulkan=true,.locations=true,.ubo_binding=1,.tex_binding=2});assert(!strcmp(mstring_get_str(source),mstring_get_str(other)));mstring_unref(other);}if(test>=77824 && test<79360 && (test-77824)%3!=2){PshState canonical=state;for(unsigned j=0;j<4;j++){canonical.dim_tex[j]=2;canonical.tex_cubemap[j]=false;}MString *other=pgraph_glsl_gen_psh(&canonical,(GenPshGlslOptions){.vulkan=true,.locations=true,.ubo_binding=1,.tex_binding=2});assert(!strcmp(mstring_get_str(source),mstring_get_str(other)));mstring_unref(other);}reference_source_bytes+=strlen(mstring_get_str(source));mstring_unref(source);
  words[0]+=modules[0].num_words;words[1]+=modules[1].num_words;
  uint32_t expected[80];uint64_t build_ns[2]={0},draw_ns[2]={0};
  for(unsigned path=0;path<2;path++){
   assert(modules[path].uniforms.size<=2048);memset(uniform.mapped,0,2048);pack(uniform.mapped,&modules[path].uniforms,&values);VkShaderModule frag=module(&modules[path]);
   VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertices[test>=54272?0:test>=52224?3:test>=48128?2:test>=44032?0:test>=37888?2:test>=18944][state.smooth_shading],.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
   VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=state.point_sprite?VK_PRIMITIVE_TOPOLOGY_POINT_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkViewport viewport={0,0,4,4,0,1};VkRect2D scissor={{0,0},{4,4}};VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_ALWAYS};VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,.pColorBlendState=&cb,.layout=layout,.renderPass=pass};VkPipeline pipeline;if(!timings && !numerical_cache){VkPipelineCacheCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};CHECK(vkCreatePipelineCache(device,&ci,NULL,&numerical_cache));}uint64_t build_start=timings?wall_ns():0;CHECK(vkCreateGraphicsPipelines(device,timings?VK_NULL_HANDLE:numerical_cache,1,&gp,NULL,&pipeline));if(timings)build_ns[path]=wall_ns()-build_start;
   CHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));VkClearValue clear[2]={{{{-2,3,5,7}}}, {.depthStencil={1,0}}};VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{4,4}},.clearValueCount=2,.pClearValues=clear};vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,128,data);vkCmdDraw(cmd,state.point_sprite?1:3,1,0,0);vkCmdEndRenderPass(cmd);
   VkBufferImageCopy copies[2]={{.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}},{.bufferOffset=256,.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={4,4,1}}};vkCmdCopyImageToBuffer(cmd,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,copies);vkCmdCopyImageToBuffer(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,copies+1);CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};uint64_t draw_start=timings?wall_ns():0;CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));if(timings)draw_ns[path]=wall_ns()-draw_start;
   if(!path)memcpy(expected,readback.mapped,320);else for(unsigned j=0;j<80;j++){uint32_t got;float x,y;memcpy(&got,readback.mapped+j*4,4);memcpy(&x,&expected[j],4);memcpy(&y,&got,4);compared++;if(got!=expected[j] && !(isnan(x)&&isnan(y))){if(failures++<25)fprintf(stderr,"case %u output %u: %08x (%g) != %08x (%g)\n",test,j,expected[j],x,got,y);}}
   vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,frag,NULL);
  }
  if(timings)fprintf(stderr,"PSH timing case=%u ref_build_ns=%llu native_build_ns=%llu ref_submit_fence_ns=%llu native_submit_fence_ns=%llu UNQUALIFIED\n",test,(unsigned long long)build_ns[0],(unsigned long long)build_ns[1],(unsigned long long)draw_ns[0],(unsigned long long)draw_ns[1]);
  bool sampled=test>=70656 && test<79360;for(unsigned j=0;j<4;j++)sampled|=((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_PROJECT2D || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_PROJECT3D || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_CUBEMAP || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_BUMPENVMAP || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_BUMPENVMAP_LUM || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_DPNDNT_AR || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_DPNDNT_GB;
  if(test==54272){float red;memcpy(&red,expected,4);assert(isfinite(red) && red>0.0f && red<1.0f);printf("Signed fixture signal: first_red=%.9g; final constant bank active\n",red);}
  if(test==60416){float red;memcpy(&red,expected,4);assert(isfinite(red) && red>0.0f && red<1.0f);printf("Raw integer fixture signal: first_red=%.9g; numeric conversion and final constant bank active\n",red);}
  if(test==70656){unsigned signal=0;for(unsigned p=0;p<16;p++){float red;memcpy(&red,expected+p*4,4);signal+=isfinite(red)&&red>=.125f&&red<=.5f;}assert(signal);printf("Integer dot fixture signal: %u nontrivial scaled integer pixels\n",signal);}
  if(test>=74752 && test<76288)for(unsigned p=0;p<16;p++){float red;memcpy(&red,expected+p*4,4);metadata_signal+=isfinite(red)&&red>0.0f&&red<1.0f;}
  if(test>=77824 && test<79360 && (test-77824)%3==1)for(unsigned p=0;p<16;p++){float red;memcpy(&red,expected+p*4,4);sampler_signal+=isfinite(red)&&red>0.0f&&red<1.0f;}
  if(test>=79360 && test<80656)for(unsigned p=0;p<16;p++){float red;memcpy(&red,expected+p*4,4);composition_signal+=isfinite(red)&&red>0.0f&&red<1.0f;}
  if(test>=80656)for(unsigned p=0;p<16;p++){float red;memcpy(&red,expected+p*4,4);alias_signal+=isfinite(red)&&red>0.0f&&red<1.0f;}
  for(unsigned j=0;j<16;j++){if(expected[j*4]==0xc0000000u)discarded++;else{written++;sample_pixels+=sampled;point_pixels+=state.point_sprite;}}
  release(&modules[0]);release(&modules[1]);
 }
 printf("Fragment GPU: %u scalar color/depth/discard results, %u differences, %u validation errors; validation=%s\n",compared,failures,errors,validated?"enabled":"DISABLED");
 if(!defer_coverage)verify_coverage(first,limit,written,discarded,sample_pixels,point_pixels,metadata_signal,sampler_signal,composition_signal,alias_signal);
 printf("PSH_COVERAGE %u %u %u %u %u %u %u %u %u %u\n",first,limit,written,discarded,sample_pixels,point_pixels,metadata_signal,sampler_signal,composition_signal,alias_signal);
 printf("Fragment coverage: written_pixels=%u discarded_pixels=%u sampled_pixels=%u point_pixels=%u\n",written,discarded,sample_pixels,point_pixels);
 printf("Fragment work: reference_words=%llu native_words=%llu avoided_GLSL_bytes=%llu; work counts only, no latency/GPU execution claim\n",(unsigned long long)words[0],(unsigned long long)words[1],(unsigned long long)reference_source_bytes);
 for(unsigned j=0;j<4;j++){for(unsigned k=0;k<2;k++)if(vertices[j][k])vkDestroyShaderModule(device,vertices[j][k],NULL);}
 vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,commands,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);drop_image(color);drop_image(depth);drop(uniform);drop(readback);for(unsigned j=0;j<16;j++)vkDestroySampler(device,samplers[j],NULL);for(unsigned j=0;j<3;j++)if(depth_views[j])vkDestroyImageView(device,depth_views[j],NULL);if(signed_gb.image)drop_image(signed_gb);if(encoded_f16.image)drop_image(encoded_f16);if(packed_depth.image)drop_image(packed_depth);drop_image(texture);drop_image(volume);drop_image(cube);if(numerical_cache)vkDestroyPipelineCache(device,numerical_cache,NULL);vkDestroyDevice(device,NULL);if(validated)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);return failures||errors?1:0;
}
