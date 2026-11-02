/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "vulkan/vulkan_core.h"
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define MAX(a,b) ((a)>(b)?(a):(b))
enum {BUFFER_COMPUTE_DST, BUFFER_COMPUTE_SRC};
typedef struct {VkBuffer buffer; VkDeviceSize buffer_size;} StorageBuffer;
typedef struct {StorageBuffer storage_buffers[2]; VkPhysicalDeviceProperties device_props;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state; unsigned scale;} PGRAPHState;
typedef struct {unsigned width,height,pitch;struct {unsigned bytes_per_pixel;} fmt;} SurfaceBinding;
static VkDeviceSize requested;
static unsigned requests;
static void pgraph_apply_scaling_factor(PGRAPHState *p, unsigned *w, unsigned *h)
{*w*=p->scale;*h*=p->scale;}
static void pgraph_vk_buffer_ensure_capacity(PGRAPHState *p, unsigned buffer, VkDeviceSize needed)
{
    assert(buffer==BUFFER_COMPUTE_DST);requested=needed;requests++;
    for(unsigned i=0;i<2;i++) if(p->vk_renderer_state->storage_buffers[i].buffer_size<needed)
        p->vk_renderer_state->storage_buffers[i].buffer_size=needed;
}
static unsigned barrier_calls,unpack_calls,recorded_count;
static VkBufferMemoryBarrier recorded[5];
static void checked_barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,
    VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t nm,
    const VkMemoryBarrier *mem,uint32_t nb,const VkBufferMemoryBarrier *buf,
    uint32_t ni,const VkImageMemoryBarrier *im)
{
    assert(cmd==(VkCommandBuffer)(uintptr_t)4&&!flags&&!nm&&!mem&&!ni&&!im);
    if(barrier_calls==0){
        assert(!unpack_calls&&nb==1&&src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==src);
        assert(buf[0].buffer==(VkBuffer)(uintptr_t)3);
    }else{
        assert(nb==2&&buf[0].buffer==(VkBuffer)(uintptr_t)1&&buf[1].buffer==(VkBuffer)(uintptr_t)2);
        if(barrier_calls==1)assert(!unpack_calls&&src==VK_PIPELINE_STAGE_TRANSFER_BIT&&dst==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        else assert(barrier_calls==2&&unpack_calls==1&&src==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    assert(recorded_count+nb<=5);
    for(unsigned i=0;i<nb;i++)recorded[recorded_count++]=buf[i];
    barrier_calls++;
}
static void checked_unpack(PGRAPHState *pg,SurfaceBinding *s,VkCommandBuffer cmd,VkBuffer src,VkBuffer dst)
{
    assert(pg&&s&&cmd==(VkCommandBuffer)(uintptr_t)4&&barrier_calls==2&&!unpack_calls);
    assert(src==(VkBuffer)(uintptr_t)1&&dst==(VkBuffer)(uintptr_t)2);
    unpack_calls++;
}
#define vkCmdPipelineBarrier checked_barrier
#define pgraph_vk_unpack_depth_stencil checked_unpack
/* HELPER */
static void check(unsigned width,unsigned height,unsigned scale,VkDeviceSize alignment,
                  unsigned *old_short,unsigned *growth_avoided,bool from_vram,VkDeviceSize staged_offset)
{
    const VkDeviceSize capacity=800ull*1024*1024;
    PGRAPHVkState state={.storage_buffers={{(VkBuffer)(uintptr_t)1,capacity},
                                         {(VkBuffer)(uintptr_t)2,capacity}}};
    state.device_props.limits.minStorageBufferOffsetAlignment=alignment;
    PGRAPHVkState *r=&state;
    PGRAPHState graph={r,scale},*pg=&graph;
    SurfaceBinding source={width,height,width*4+64,{4}},*surface=&source;
    VkDeviceSize row_bytes=(VkDeviceSize)width*4;
    VkBuffer source_buffer=(VkBuffer)(uintptr_t)3;
    bool use_compute_to_convert_depth_stencil_format=true;
    unsigned scaled_width=width*scale,scaled_height=height*scale;
    VkCommandBuffer cmd=(VkCommandBuffer)(uintptr_t)4;
    VkBufferImageCopy regions[2]={{0}};
    requests=barrier_calls=unpack_calls=recorded_count=0;
    /* PLAN */
    VkDeviceSize pixels=(VkDeviceSize)width*height*scale*scale;
    VkDeviceSize stencil_start=((pixels*4+alignment-1)/alignment)*alignment;
    VkDeviceSize word_end=stencil_start+((pixels+3)/4)*4;
    VkDeviceSize packed_input=(VkDeviceSize)width*height*4;
    assert(barrier_calls==3&&unpack_calls==1&&recorded_count==5);
    const VkBufferMemoryBarrier *expected[]={&post_copy_src_barrier,
        &pre_unpack_src_barrier,&pre_unpack_dst_barrier,
        &post_unpack_src_barrier,&post_unpack_dst_barrier};
    for(unsigned i=0;i<5;i++){
        assert(recorded[i].sType==expected[i]->sType);
        assert(recorded[i].buffer==expected[i]->buffer&&recorded[i].offset==expected[i]->offset&&recorded[i].size==expected[i]->size);
        assert(recorded[i].srcAccessMask==expected[i]->srcAccessMask&&recorded[i].dstAccessMask==expected[i]->dstAccessMask);
        assert(recorded[i].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&recorded[i].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    }
    assert(requests==1&&requested==(word_end>packed_input?word_end:packed_input));
    assert(regions[1].bufferOffset==stencil_start);
    assert(pre_unpack_dst_barrier.size==word_end&&post_unpack_dst_barrier.size==word_end);
    assert(pre_unpack_dst_barrier.buffer==unpack_buffer->buffer&&post_unpack_dst_barrier.buffer==unpack_buffer->buffer);
    assert(pre_unpack_dst_barrier.srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT);
    assert(pre_unpack_dst_barrier.dstAccessMask==VK_ACCESS_SHADER_WRITE_BIT);
    assert(post_unpack_dst_barrier.srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT);
    assert(post_unpack_dst_barrier.dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT);
    assert(unpack_buffer->buffer_size>=word_end);
    assert(post_copy_src_barrier.buffer==source_buffer&&post_copy_src_barrier.offset==staged_offset);
    assert(post_copy_src_barrier.size==(VkDeviceSize)(height-1)*(width*4+(from_vram?64:0))+width*4);
    assert(pre_unpack_src_barrier.buffer==r->storage_buffers[BUFFER_COMPUTE_DST].buffer&&post_unpack_src_barrier.buffer==pre_unpack_src_barrier.buffer);
    assert(!pre_unpack_src_barrier.offset&&!post_unpack_src_barrier.offset);
    assert(pre_unpack_src_barrier.size==packed_input&&post_unpack_src_barrier.size==packed_input);
    assert(pre_unpack_src_barrier.srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&pre_unpack_src_barrier.dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
    assert(post_unpack_src_barrier.srcAccessMask==VK_ACCESS_SHADER_READ_BIT&&post_unpack_src_barrier.dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
    if(pixels*5<word_end)(*old_short)++;
    if(pixels*8>capacity&&requested<=capacity)(*growth_avoided)++;
}
static void check_copy_source(unsigned width,unsigned height,unsigned bpp,
    unsigned padding,VkDeviceSize staged_offset,bool depth)
{
    SurfaceBinding source={width,height,(width+padding)*bpp,{bpp}},*surface=&source;
    unsigned staged_row_length=padding?width+padding:0;
    VkBuffer source_buffer=(VkBuffer)(uintptr_t)5;
    VkDeviceSize unpacked_size=depth_stencil_plane_size(width*3,height*3,256);
    /* SOURCE REUSE */
    VkDeviceSize expected_offset=depth?0:staged_offset;
    VkDeviceSize expected_size=depth?unpacked_size:
        (VkDeviceSize)height*width*bpp+(VkDeviceSize)(height-1)*padding*bpp;
    const VkBufferMemoryBarrier *actual[]={&direct_source_barrier,&scratch_source_barrier};
    for(unsigned i=0;i<2;i++){
        assert(actual[i]->buffer==source_buffer&&actual[i]->offset==expected_offset&&actual[i]->size==expected_size);
        assert(actual[i]->srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&actual[i]->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
        assert(actual[i]->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&actual[i]->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    }
}
int main(void)
{
    const unsigned widths[]={1,2,3,7,640,1920},heights[]={1,2,3,480,1080};
    const VkDeviceSize alignments[]={1,16,256,4096};
    unsigned cases=0,old_short=0,growth_avoided=0;
    for(unsigned w=0;w<6;w++)for(unsigned h=0;h<5;h++)
    for(unsigned scale=1;scale<=8;scale*=2)for(unsigned a=0;a<4;a++)
    for(unsigned imported=0;imported<2;imported++)for(unsigned offset=0;offset<3;offset++){
        check(widths[w],heights[h],scale,alignments[a],&old_short,&growth_avoided,imported,offset==0?0:offset==1?16:65536);cases++;
    }
    unsigned reuse_cases=0;
    for(unsigned w=0;w<6;w++)for(unsigned h=0;h<5;h++)
    for(unsigned bpp=1;bpp<=4;bpp*=2)for(unsigned pad=0;pad<2;pad++)
    for(unsigned offset=0;offset<3;offset++)for(unsigned depth=0;depth<2;depth++){
        if(depth&&bpp!=4)continue;
        check_copy_source(widths[w],heights[h],bpp,pad*16,offset==0?0:offset==1?16:65536,depth);
        reuse_cases++;
    }
    printf("Surface upload reuse: %u tight/pitched/offset/depth cases check both direct and scratch image paths\n",reuse_cases);
    assert(old_short&&growth_avoided);
    printf("Depth unpack: %u pre/post paired barrier schedules passed (5 -> 3 calls per upload)\n",cases);
    printf("depth upload: %u footprint/barrier/copy-offset cases; %u old short ranges corrected, %u oversized growth requests avoided\n",
           cases,old_short,growth_avoided);
}
