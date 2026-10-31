/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <volk.h>
#include "ui/libretro-crosshair.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
XemuLibretroCrosshair xemu_libretro_crosshairs[2];
/* QUIET ERRORS */
/* GPU HELPERS */
/* PRODUCTION */
/* Independent pixel reference: original transfer path's byte colors and bars. */
static void reference(uint32_t *pixels,int w,int h) {
 const uint32_t colors[]={0xff000000,0xff2828ff,0xffff8c28};
 int arm=MIN(MAX(h/40,6),40),thick=MIN(MAX(h/240,2),6);
 for(int pass=0;pass<2;pass++)for(int p=0;p<2;p++) {
  XemuLibretroCrosshair *c=&xemu_libretro_crosshairs[p];if(!c->shown)continue;
  int cx=(int)(c->x*w),cy=(int)(c->y*h),edge=pass?0:1;
  int len=2*arm+1+2*edge,t=thick+2*edge;
  for(int axis=0;axis<2;axis++) {
   int x=axis?cx-t/2:cx-arm-edge,y=axis?cy-arm-edge:cy-t/2;
   int rw=axis?t:len,rh=axis?len:t;
   for(int yy=MAX(y,0);yy<MIN(y+rh,h);yy++)
    for(int xx=MAX(x,0);xx<MIN(x+rw,w);xx++)pixels[yy*w+xx]=colors[pass?1+p:0];
  }
 }
}
static void cases(int w,int h) {
 VkImage image;VkDeviceMemory memory;VkImageView view;VkFramebuffer fb;VkRenderPass pass;
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
 CHECK(vkCreateImage(device,&ci,NULL,&image));VkMemoryRequirements mr;vkGetImageMemoryRequirements(device,image,&mr);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=memory_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
 CHECK(vkAllocateMemory(device,&ai,NULL,&memory));CHECK(vkBindImageMemory(device,image,memory,0));
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 CHECK(vkCreateImageView(device,&vi,NULL,&view));
 VkAttachmentDescription attachment={.format=ci.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
 VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};
 VkRenderPassCreateInfo pi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&sub};CHECK(vkCreateRenderPass(device,&pi,NULL,&pass));
 VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&view,.width=w,.height=h,.layers=1};CHECK(vkCreateFramebuffer(device,&fi,NULL,&fb));
 Buffer output=make_buffer((size_t)w*h*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);uint32_t *expected=malloc((size_t)w*h*4);assert(expected);
 for(unsigned i=0;i<64;i++) {
  xemu_libretro_crosshairs[0]=(XemuLibretroCrosshair){i&1,(i%8)/7.0f,(i/8)/7.0f};
  xemu_libretro_crosshairs[1]=(XemuLibretroCrosshair){i&2,1-(i%8)/7.0f,1-(i/8)/7.0f};
  if(i>=48)xemu_libretro_crosshairs[1]=xemu_libretro_crosshairs[0];
  if(i==63){xemu_libretro_crosshairs[0]=(XemuLibretroCrosshair){true,-.02f,1.02f};xemu_libretro_crosshairs[1]=(XemuLibretroCrosshair){true,1.02f,-.02f};}
  for(int p=0;p<w*h;p++)expected[p]=0xff332211;
  reference(expected,w,h);begin();
  VkClearValue background={.color.float32={17.0f/255,34.0f/255,51.0f/255,1}};
  VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{w,h}},.clearValueCount=1,.pClearValues=&background};
  vkCmdBeginRenderPass(command,&bi,VK_SUBPASS_CONTENTS_INLINE);
  VkRect2D tiny={{0,0},{1,1}};vkCmdSetScissor(command,0,1,&tiny);
  draw_crosshairs(command,w,h);vkCmdEndRenderPass(command);
  frame_image_barrier(command,image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
  vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.buffer,1,&copy);host_barrier(output,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);submit();
  assert(!memcmp(expected,output.mapped,(size_t)w*h*4));gpu_cases++;
 }
 free(expected);drop_buffer(output);vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyImageView(device,view,NULL);vkDestroyImage(device,image,NULL);vkFreeMemory(device,memory,NULL);
}
int main(void) {
 quiet_test_errors();gpu_init();cases(1,1);cases(37,23);cases(640,480);cases(1280,720);cases(97,2160);cases(83,3840);
 printf("Crosshair GPU: %u pixel-exact cases passed, clipping, overlap, colors, hidden players and restricted scissor\n",gpu_cases);
 vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
 if(messenger)((PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,NULL);
 vkDestroyInstance(instance,NULL);return 0;
}
