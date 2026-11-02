/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/scanout.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP(v,a) (((v)+(a)-1)&~((a)-1))
typedef struct {uint8_t*vram_ptr;} NV2AState;
static unsigned copies,claims;static uint64_t claimed_offset[64],claimed_size[64];
static void lr_gpu_ram_claim(uintptr_t cmd,uint64_t offset,uint64_t size){assert(cmd==7&&claims<64);claimed_offset[claims]=offset;claimed_size[claims++]=size;}
static void memcpy_image(void*dst,const void*src,size_t dst_pitch,size_t src_pitch,size_t height){copies++;for(size_t y=0;y<height;y++)memcpy((uint8_t*)dst+y*dst_pitch,(const uint8_t*)src+y*src_pitch,dst_pitch);}
static VkDeviceSize source_begin,source_end,expected_begin,expected_end;
static unsigned pre_barriers,post_barriers;
static void pgraph_vk_upload_source_barrier(void *pg,VkCommandBuffer cmd,bool direct,VkDeviceSize offset,VkDeviceSize size)
{
 assert(pg==(void*)(uintptr_t)1&&cmd==(VkCommandBuffer)(uintptr_t)7);
 assert(offset==expected_begin&&size==expected_end-expected_begin);
 pre_barriers+=direct;
}
static void pre_barrier(const PGRAPHScanout *scan,bool from_vram)
{
 void *pg=(void*)(uintptr_t)1;VkCommandBuffer cmd=(VkCommandBuffer)(uintptr_t)7;
 /* PRE BARRIER */
}
static void checked_barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,
 VkDependencyFlags flags,uint32_t nm,const VkMemoryBarrier *m,uint32_t nb,const VkBufferMemoryBarrier *b,uint32_t ni,const VkImageMemoryBarrier *im)
{
 assert(cmd==(VkCommandBuffer)(uintptr_t)7&&!flags&&!nm&&!m&&nb==1&&!ni&&!im);
 assert(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
 assert(b->buffer==(VkBuffer)(uintptr_t)9&&b->offset==expected_begin&&b->size==expected_end-expected_begin);
 assert(b->srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&b->dstAccessMask==(VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT));
 assert(b->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&b->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
 post_barriers++;
}
#define vkCmdPipelineBarrier checked_barrier
static void post_barrier(void)
{
 VkCommandBuffer cmd=(VkCommandBuffer)(uintptr_t)7;VkBuffer source=(VkBuffer)(uintptr_t)9;
 /* POST BARRIER */
}
static VkDeviceSize build(NV2AState*d,VkCommandBuffer cmd,const PGRAPHScanout*scan,PGRAPHScanoutRect*rects,unsigned count,VkBufferImageCopy*regions,bool from_vram,uint8_t*staged,VkDeviceSize staged_offset,VkDeviceSize staged_used){
/* PRODUCTION */
 return staged_used;
}
static uint8_t ram[65536],staging[65536],expected[65536];
static uint32_t seed=0x74ae613;
static unsigned random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 unsigned schedules=0,rectangles=0;
 for(unsigned trial=0;trial<2000;trial++)for(unsigned bpp=2;bpp<=4;bpp+=2)for(unsigned direct=0;direct<2;direct++){
  for(unsigned i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)(i*17+trial);
  memset(staging,0xcd,sizeof(staging));memset(expected,0xcd,sizeof(expected));
  NV2AState d={ram};PGRAPHScanout scan={.start=16+4*(trial%4),.pitch=64*bpp+8*(trial%3),.bpp=bpp,.width=64,.height=64};
  uint64_t used=(trial%8)*16,reference=used,base=(trial%4)*256;
  expected_begin=scan.start;expected_end=scan.start+63*scan.pitch+64*bpp;
  pre_barrier(&scan,direct);source_begin=UINT64_MAX;source_end=0;
  uint64_t reference_begin=UINT64_MAX,reference_end=0;
  for(unsigned batch=0;batch<4;batch++){
   PGRAPHScanoutRect rects[64];VkBufferImageCopy regions[64];unsigned count=1+random_word()%64;
   for(unsigned i=0;i<count;i++)rects[i]=(PGRAPHScanoutRect){.dx=random_word()%56,.dy=random_word()%60,.width=1+random_word()%8,.height=1+random_word()%4};
   copies=claims=0;used=build(&d,(VkCommandBuffer)(uintptr_t)7,&scan,rects,count,regions,direct,staging+16,base,used);
   assert(copies==(direct?0:count)&&claims==(direct?count:0));
   for(unsigned i=0;i<count;i++){
    const PGRAPHScanoutRect*p=&rects[i];uint64_t src=scan.start+(uint64_t)p->dy*scan.pitch+p->dx*bpp;
    uint64_t expected_offset=direct?src:base+reference;
    uint64_t span=direct?(uint64_t)(p->height-1)*scan.pitch+p->width*bpp:(uint64_t)p->width*p->height*bpp;
    if(expected_offset<reference_begin)reference_begin=expected_offset;
    if(expected_offset+span>reference_end)reference_end=expected_offset+span;
    assert(regions[i].bufferOffset==expected_offset&&regions[i].bufferRowLength==(direct?scan.pitch/bpp:0)&&!regions[i].bufferImageHeight);
    assert(regions[i].imageOffset.x==(int)p->dx&&regions[i].imageOffset.y==(int)p->dy&&!regions[i].imageOffset.z);
    assert(regions[i].imageExtent.width==p->width&&regions[i].imageExtent.height==p->height&&regions[i].imageExtent.depth==1);
    assert(regions[i].imageSubresource.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&!regions[i].imageSubresource.mipLevel&&!regions[i].imageSubresource.baseArrayLayer&&regions[i].imageSubresource.layerCount==1);
    if(direct){assert(claimed_offset[i]==src&&claimed_size[i]==(uint64_t)(p->height-1)*scan.pitch+p->width*bpp);}
    else{
     for(unsigned y=0;y<p->height;y++)for(unsigned x=0;x<p->width*bpp;x++)expected[16+reference+y*p->width*bpp+x]=ram[src+y*scan.pitch+x];
     reference+=((uint64_t)p->width*p->height*bpp+15)/16*16;
    }
   }
   assert(used==reference&&!memcmp(staging,expected,sizeof(staging)));rectangles+=count;
  }
  expected_begin=reference_begin;expected_end=reference_end;post_barrier();
  schedules++;
 }
 assert(pre_barriers==4000&&post_barriers==8000);
 printf("Scanout ranges: 4000 frame-bounded direct visibility barriers and 8000 exact source envelopes passed\n");
 printf("PASS: %u upload schedules / %u rectangles; exact pitched bytes, aligned staging offsets, direct-RAM ownership and guarded batches\n",schedules,rectangles);
}
