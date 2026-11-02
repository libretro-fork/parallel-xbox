/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "vulkan/vulkan_core.h"
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define BUFFER_COMPUTE_DST 0
#define NV2A_PROF_SURF_DOWNLOAD 0
#define nv2a_profile_inc_counter(...) ((void)0)
#define pgraph_vk_swizzle_possible(...) false
#define memory_region_size(p) (*(p))
typedef struct {
    VkBuffer vram_buffer;
    struct {VkDeviceSize buffer_size;} storage_buffers[1];
    struct {struct {VkDeviceSize minStorageBufferOffsetAlignment;} limits;} device_props;
    struct {bool open;} readback_batch;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state; unsigned scale;} PGRAPHState;
typedef struct {PGRAPHState pgraph; uint8_t *vram_ptr; size_t *vram;} NV2AState;
typedef struct {
    unsigned width,height,pitch; bool color,swizzle; uint64_t vram_addr;
    struct {unsigned bytes_per_pixel;} fmt;
    struct {VkFormat vk_format; unsigned host_bytes_per_pixel;} host_fmt;
} SurfaceBinding;
static VkDeviceSize observed_size;
static void pgraph_apply_scaling_factor(PGRAPHState *p,unsigned *w,unsigned *h)
{*w*=p->scale;*h*=p->scale;}
static VkDeviceSize expected_barrier_size,expected_barrier_offset;
static unsigned barrier_checks;
static void checked_barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,
    VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t nm,
    const VkMemoryBarrier *mem,uint32_t nb,const VkBufferMemoryBarrier *buf,
    uint32_t ni,const VkImageMemoryBarrier *im)
{
    assert(cmd==(VkCommandBuffer)(uintptr_t)2&&nb==1&&!nm&&!ni&&!flags&&!mem&&!im);
    assert(src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT);
    assert(buf->buffer==(VkBuffer)(uintptr_t)3&&buf->offset==expected_barrier_offset&&buf->size==expected_barrier_size);
    assert(buf->srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&buf->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
    barrier_checks++;
}
#define vkCmdPipelineBarrier checked_barrier
/* PRODUCTION */
int main(void)
{
    const VkFormat formats[]={VK_FORMAT_D16_UNORM,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT};
    const unsigned dimensions[][2]={{1,1},{640,480},{1920,1080}};
    const VkDeviceSize alignments[]={1,256,4096};
    unsigned cases=0,retained=0,rejected_short=0;
    for(unsigned f=0;f<3;f++)for(unsigned dim=0;dim<3;dim++)
    for(unsigned scale=1;scale<=8;scale*=2)for(unsigned a=0;a<3;a++)
    for(unsigned cap=0;cap<3;cap++)for(unsigned open=0;open<2;open++)
    for(unsigned imported=0;imported<2;imported++){
        unsigned w=dimensions[dim][0],h=dimensions[dim][1];
        VkDeviceSize pixels=(VkDeviceSize)w*h*scale*scale;
        VkDeviceSize plane=((pixels*4+alignments[a]-1)/alignments[a])*alignments[a]+((pixels+3)/4)*4;
        VkDeviceSize required=f?plane:0;
        VkDeviceSize capacity=cap==0?(required?required-1:0):cap==1?required:800ull*1024*1024;
        PGRAPHVkState r={.vram_buffer=imported?(VkBuffer)(uintptr_t)1:VK_NULL_HANDLE,
            .storage_buffers={{capacity}},.readback_batch={open}};
        r.device_props.limits.minStorageBufferOffsetAlignment=alignments[a];
        unsigned char ram=0;
        size_t ram_size=64*1024*1024;
        NV2AState d={.pgraph={&r,scale},.vram_ptr=&ram,.vram=&ram_size};
        SurfaceBinding s={.width=w,.height=h,.pitch=w*(f?4:2),.fmt={f?4:2},
            .host_fmt={formats[f],f==0?2:f==1?4:8}};
        bool batched=download_surface_to_buffer(&d,&s,&ram);
        bool expected=imported&&open&&(!f||required<=capacity);
        bool previous=imported&&open&&(!f||pixels*8<=capacity);
        assert(observed_size==required&&batched==expected);
        VkBufferImageCopy region={.bufferOffset=a*256,.imageExtent={w,h,1}};
        expected_barrier_offset=region.bufferOffset;
        expected_barrier_size=f?plane:(VkDeviceSize)w*h*2;
        record_destination(&s,f!=0,observed_size,&region,
            (VkBuffer)(uintptr_t)3,(VkCommandBuffer)(uintptr_t)2);
        if(batched&&!previous)retained++;
        if(previous&&!batched)rejected_short++;
        cases++;
    }
    assert(retained&&rejected_short&&barrier_checks==cases);
    printf("Readback destination ranges: %u native D16/aligned scaled depth plane checks passed\n",barrier_checks);
    printf("depth readback: %u format/scale/alignment/capacity/batch/import cases; %u unnecessary batch flush decisions avoided, %u short-capacity batches rejected\n",cases,retained,rejected_short);
}
