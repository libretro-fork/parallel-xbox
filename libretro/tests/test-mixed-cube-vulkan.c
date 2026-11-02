/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) assert((x)==VK_SUCCESS)
#define NV2A_PROF_SURF_TO_TEX 0
#define RGBA_GREEN 0
/* GPU COMMON */
typedef struct {unsigned color_format;} TextureShape;
typedef struct {VkImage image;VkImageLayout current_layout;struct {TextureShape state;}key;unsigned cube_source_mask,cube_source_draw_time[6];int draw_time;} TextureBinding;
typedef struct {VkImage image;unsigned width,height;bool color,in_shader_read;int draw_time;struct {VkFormat vk_format;VkImageAspectFlags aspect;}host_fmt;} SurfaceBinding;
typedef struct {unsigned unused;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;unsigned surface_scale_factor;} PGRAPHState;
typedef struct {VkFormat vk_format;} VkColorFormatInfo;
static VkCommandBuffer active_cmd;
static unsigned recordings,ends,copies;
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r,unsigned format){return (VkColorFormatInfo){VK_FORMAT_R8G8B8A8_UNORM};}
static void nv2a_profile_inc_counter(unsigned c){assert(c==0);}
static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg){recordings++;return active_cmd;}
static void pgraph_vk_end_nondraw_commands(PGRAPHState *pg,VkCommandBuffer cmd){assert(cmd==active_cmd);ends++;}
static void pgraph_vk_begin_debug_marker(PGRAPHVkState *r,VkCommandBuffer cmd,unsigned color,const char *name){assert(cmd==active_cmd);}
static void pgraph_vk_end_debug_marker(PGRAPHVkState *r,VkCommandBuffer cmd){assert(cmd==active_cmd);}
static VkImageLayout pgraph_vk_surface_base_layout(SurfaceBinding *s){return s->in_shader_read?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
static void pgraph_vk_surface_left_shader_read(PGRAPHState *pg,SurfaceBinding *s){s->in_shader_read=false;}
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *w,unsigned *h){*w*=pg->surface_scale_factor;*h*=pg->surface_scale_factor;}
static void tracked_copy(VkCommandBuffer cmd,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t n,const VkImageCopy *regions){copies+=n;vkCmdCopyImage(cmd,src,sl,dst,dl,n,regions);}
static bool count_source_barriers;
static VkImage target_image;
static VkImageLayout target_before;
static unsigned total_barriers;
static unsigned source_batches,source_images,expected_sources;
static VkPipelineStageFlags expected_stages;
static void tracked_barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t nm,const VkMemoryBarrier *m,uint32_t nb,const VkBufferMemoryBarrier *b,uint32_t ni,const VkImageMemoryBarrier *im){
 if(count_source_barriers)total_barriers++;
 if(count_source_barriers&&ni&&im[0].image!=target_image){
  bool before=im[0].newLayout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  unsigned extra=before?(target_before!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL):1;
  assert(!flags&&!nm&&!nb&&ni==expected_sources+extra);
  assert(src==(before?expected_stages:VK_PIPELINE_STAGE_TRANSFER_BIT));
  assert(dst==(before?VK_PIPELINE_STAGE_TRANSFER_BIT:(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)));
  unsigned targets=0;
  for(unsigned i=0;i<ni;i++){
   assert(im[i].subresourceRange.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT);
   if(im[i].image==target_image){
    targets++;assert(im[i].dstAccessMask==(before?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT));
    assert(im[i].srcAccessMask==(before?(target_before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_SHADER_READ_BIT):VK_ACCESS_TRANSFER_WRITE_BIT));
   }else{
    assert(im[i].dstAccessMask==(before?VK_ACCESS_TRANSFER_READ_BIT:(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)));
    source_images++;
   }
   for(unsigned j=0;j<i;j++)assert(im[i].image!=im[j].image);
  }
  assert(targets==extra);source_batches++;
 }
 vkCmdPipelineBarrier(cmd,src,dst,flags,nm,m,nb,b,ni,im);
}
#define vkCmdPipelineBarrier tracked_barrier
#define vkCmdCopyImage tracked_copy
/* PRODUCTION */
typedef struct {VkImage image;VkDeviceMemory memory;} Image;
static Image image(unsigned size,unsigned layers){
 Image out={0};VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=layers==6?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={size,size,1},.mipLevels=1,.arrayLayers=layers,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
 CHECK(vkCreateImage(device,&ci,NULL,&out.image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,out.image,&req);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,0)};
 CHECK(vkAllocateMemory(device,&ai,NULL,&out.memory));CHECK(vkBindImageMemory(device,out.image,out.memory,0));return out;
}
static void clear_face(VkCommandBuffer cmd,VkImage image,unsigned layer,unsigned value){
 VkClearColorValue color={.float32={value/255.0f,0,(255-value)/255.0f,1}};
 VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,layer,1};
 vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
}
static void verify(const uint8_t *bytes,unsigned size,unsigned layer,unsigned value){
 for(unsigned i=0;i<size*size;i++){
  const uint8_t *p=bytes+((size_t)layer*size*size+i)*4;
  assert(p[0]==value&&p[1]==0&&p[2]==255-value&&p[3]==255);
 }
}
int main(void){
/* GPU INIT */
 active_cmd=cmd;PGRAPHVkState renderer={0};unsigned cases=0;size_t pixels=0;
 for(unsigned scale=1;scale<=4;scale*=2)for(unsigned mask=1;mask<64;mask++)
 for(unsigned changed=0;changed<2;changed++)for(unsigned fresh=0;fresh<2;fresh++)for(unsigned alias=0;alias<2;alias++){
  unsigned size=8*scale,first=__builtin_ctz(mask);
  PGRAPHState pg={&renderer,scale};Image sources[6],old=image(size,6),replacement={0};
  if(fresh)replacement=image(size,6);
  TextureBinding t={.image=fresh?replacement.image:old.image,.current_layout=fresh?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.cube_source_mask=mask};
  SurfaceBinding surfaces[6],*faces[6];Buffer output=buffer((size_t)size*size*6*4*(fresh?2:1));
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
  for(unsigned f=0;f<6;f++){
   sources[f]=image(size,1);surfaces[f]=(SurfaceBinding){.image=sources[f].image,.width=8,.height=8,.color=true,.in_shader_read=(f%2)!=0,.draw_time=10+(changed&&f==first),.host_fmt={VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT}};
   faces[f]=mask&(1u<<f)?&surfaces[f]:NULL;t.cube_source_draw_time[f]=10;
   pgraph_vk_transition_image_layout(&pg,cmd,sources[f].image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
   clear_face(cmd,sources[f].image,0,128+f+16*(changed&&f==first));
   pgraph_vk_transition_image_layout(&pg,cmd,sources[f].image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,surfaces[f].in_shader_read?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
  if(alias)for(unsigned f=0;f<6;f++)if(mask&(1u<<f))faces[f]=&surfaces[first];
  pgraph_vk_transition_image_layout(&pg,cmd,old.image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  for(unsigned f=0;f<6;f++)clear_face(cmd,old.image,f,(mask&(1u<<f)?128+(alias?first:f):16+f));
  pgraph_vk_transition_image_layout(&pg,cmd,old.image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  preserve_cube_upload_sources(&t,mask);
  if(!(fresh&&mask==63)){ /* A fresh all-GPU cube has no preceding RAM upload. */
  pgraph_vk_transition_image_layout(&pg,cmd,t.image,VK_FORMAT_R8G8B8A8_UNORM,t.current_layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);t.current_layout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  for(unsigned f=0;f<6;f++)if(!(mask&(1u<<f)))clear_face(cmd,t.image,f,32+f);
  if(scale==1){pgraph_vk_transition_image_layout(&pg,cmd,t.image,VK_FORMAT_R8G8B8A8_UNORM,t.current_layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);t.current_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
  }
  unsigned needed=fresh?__builtin_popcount(mask):(changed?(alias?__builtin_popcount(mask):1):0);
  copies=recordings=ends=source_batches=source_images=total_barriers=0;target_image=t.image;target_before=t.current_layout;
  expected_stages=0;expected_sources=0;
  for(unsigned f=0;f<6;f++)if(faces[f]&&(fresh||faces[f]->draw_time!=10)){
   if(alias&&expected_sources)continue;
   expected_sources++;expected_stages|=faces[f]->in_shader_read?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  }
  if(needed&&target_before!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)expected_stages|=target_before==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  count_source_barriers=true;copy_cube_faces_to_texture(&pg,faces,&t);count_source_barriers=false;
  assert(source_batches==(needed?2u:0u)&&source_images==2*expected_sources);
  assert(total_barriers==(needed?2u:(scale!=1?1u:0u)));
  assert(copies==needed&&t.current_layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&t.cube_source_mask==mask);
  assert(recordings==(unsigned)(needed||scale!=1)&&recordings==ends);
  pgraph_vk_transition_image_layout(&pg,cmd,t.image,VK_FORMAT_R8G8B8A8_UNORM,t.current_layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,6},.imageExtent={size,size,1}};
  vkCmdCopyImageToBuffer(cmd,t.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.handle,1,&region);
  if(fresh){
   pgraph_vk_transition_image_layout(&pg,cmd,old.image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
   region.bufferOffset=(size_t)size*size*6*4;vkCmdCopyImageToBuffer(cmd,old.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.handle,1,&region);
  }
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.handle,.size=VK_WHOLE_SIZE};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
  CHECK(vkEndCommandBuffer(cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));
  for(unsigned f=0;f<6;f++){
   verify(output.mapped,size,f,mask&(1u<<f)?128+(alias?first:f)+16*(changed&&(alias||f==first)):32+f);
   if(fresh)verify(output.mapped+(size_t)size*size*6*4,size,f,mask&(1u<<f)?128+(alias?first:f):16+f);
  }
  pixels+=(size_t)size*size*6*(fresh?2:1);cases++;
  vkDestroyFence(device,fence,NULL);CHECK(vkResetCommandBuffer(cmd,0));drop_buffer(output);
  for(unsigned f=0;f<6;f++){vkDestroyImage(device,sources[f].image,NULL);vkFreeMemory(device,sources[f].memory,NULL);}
  vkDestroyImage(device,old.image,NULL);vkFreeMemory(device,old.memory,NULL);
  if(fresh){vkDestroyImage(device,replacement.image,NULL);vkFreeMemory(device,replacement.memory,NULL);}
 }
 vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);if(messenger)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
 printf("Mixed cube Vulkan: %u cases, %zu exact pixels; source masks, scales, changed faces, COW image preservation and empty-recording checks; validation=%u errors=%u\n",cases,pixels,enable_validation,errors);
 return errors?1:0;
}
