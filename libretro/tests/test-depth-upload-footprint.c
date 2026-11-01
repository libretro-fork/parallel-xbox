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
typedef struct {unsigned width,height;} SurfaceBinding;
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
/* HELPER */
static void check(unsigned width,unsigned height,unsigned scale,VkDeviceSize alignment,
                  unsigned *old_short,unsigned *growth_avoided)
{
    const VkDeviceSize capacity=800ull*1024*1024;
    PGRAPHVkState state={.storage_buffers={{(VkBuffer)(uintptr_t)1,capacity},
                                         {(VkBuffer)(uintptr_t)2,capacity}}};
    state.device_props.limits.minStorageBufferOffsetAlignment=alignment;
    PGRAPHVkState *r=&state;
    PGRAPHState graph={r,scale},*pg=&graph;
    SurfaceBinding source={width,height},*surface=&source;
    bool use_compute_to_convert_depth_stencil_format=true;
    unsigned scaled_width=width*scale,scaled_height=height*scale;
    StorageBuffer *unpack_buffer=&r->storage_buffers[BUFFER_COMPUTE_SRC];
    VkBufferImageCopy regions[2]={{0}};
    requests=0;
    /* PLAN */
    VkDeviceSize pixels=(VkDeviceSize)width*height*scale*scale;
    VkDeviceSize stencil_start=((pixels*4+alignment-1)/alignment)*alignment;
    VkDeviceSize word_end=stencil_start+((pixels+3)/4)*4;
    VkDeviceSize packed_input=(VkDeviceSize)width*height*4;
    assert(requests==1&&requested==(word_end>packed_input?word_end:packed_input));
    assert(regions[1].bufferOffset==stencil_start);
    assert(pre_unpack_dst_barrier.size==word_end&&post_unpack_dst_barrier.size==word_end);
    assert(pre_unpack_dst_barrier.buffer==unpack_buffer->buffer&&post_unpack_dst_barrier.buffer==unpack_buffer->buffer);
    assert(pre_unpack_dst_barrier.srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT);
    assert(pre_unpack_dst_barrier.dstAccessMask==VK_ACCESS_SHADER_WRITE_BIT);
    assert(post_unpack_dst_barrier.srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT);
    assert(post_unpack_dst_barrier.dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT);
    assert(unpack_buffer->buffer_size>=word_end);
    if(pixels*5<word_end)(*old_short)++;
    if(pixels*8>capacity&&requested<=capacity)(*growth_avoided)++;
}
int main(void)
{
    const unsigned widths[]={1,2,3,7,640,1920},heights[]={1,2,3,480,1080};
    const VkDeviceSize alignments[]={1,16,256,4096};
    unsigned cases=0,old_short=0,growth_avoided=0;
    for(unsigned w=0;w<6;w++)for(unsigned h=0;h<5;h++)
    for(unsigned scale=1;scale<=8;scale*=2)for(unsigned a=0;a<4;a++){
        check(widths[w],heights[h],scale,alignments[a],&old_short,&growth_avoided);cases++;
    }
    assert(old_short&&growth_avoided);
    printf("depth upload: %u footprint/barrier/copy-offset cases; %u old short ranges corrected, %u oversized growth requests avoided\n",
           cases,old_short,growth_avoided);
}
