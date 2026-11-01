/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <volk.h>
#include "hw/xbox/nv2a/pgraph/vk/memory.h"
/* GPU HELPERS */

int main(void)
{
    gpu_init();
    XemuVkAllocator heap;
    assert(xemu_vk_allocator_create(physical,device,false,&heap)==VK_SUCCESS);
    CHECK(xemu_vk_allocator_reserve(heap,8u<<20,8u<<20,8u<<20));
    XemuVkMemoryRequest host={.host=true},gpu={0};
    VkBuffer buffers[2];XemuVkAllocation allocations[2];void *mapped[2];
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=65536+1024,
        .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    for(unsigned i=0;i<2;i++) {
        CHECK(xemu_vk_create_buffer(heap,&bi,&host,&buffers[i],&allocations[i]));
        CHECK(xemu_vk_map_memory(heap,allocations[i],&mapped[i]));
    }
    VkImage images[64];XemuVkAllocation image_allocations[64];
    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={64,64,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    for(unsigned i=0;i<64;i++)CHECK(xemu_vk_create_image(heap,&ii,&gpu,&images[i],&image_allocations[i]));
    for(unsigned i=0;i<64;i++) {
        memset(mapped[0],0xcd,(size_t)bi.size);memset(mapped[1],0x7e,(size_t)bi.size);
        CHECK(xemu_vk_flush_allocation(heap,allocations[0],0,VK_WHOLE_SIZE));
        CHECK(xemu_vk_flush_allocation(heap,allocations[1],0,VK_WHOLE_SIZE));
        begin();
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[i],
            .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,NULL,0,NULL,1,&barrier);
        VkClearColorValue color={.float32={(i&1)?1.f:0.f,(i&2)?1.f:0.f,(i&4)?1.f:0.f,1.f}};
        vkCmdClearColorImage(command,images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&barrier.subresourceRange);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy region={.bufferOffset=512,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={64,64,1}};
        /* The final mapped destination receives pixels directly. */
        vkCmdCopyImageToBuffer(command,images[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[0],1,&region);
        VkBufferCopy copy={.srcOffset=19,.dstOffset=37,.size=113};
        vkCmdCopyBuffer(command,buffers[1],buffers[0],1,&copy);
        VkMemoryBarrier host_barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
            0,1,&host_barrier,0,NULL,0,NULL);submit();
        CHECK(xemu_vk_invalidate_allocation(heap,allocations[0],0,VK_WHOLE_SIZE));
        for(unsigned p=0;p<bi.size;p++) {
            unsigned char expected=0xcd;
            if(p>=37&&p<150)expected=0x7e;
            if(p>=512&&p<512+64*64*4) {
                unsigned c=(p-512)%4;
                expected=(c==3||(i&(1u<<c)))?255:0;
            }
            assert(((unsigned char*)mapped[0])[p]==expected);
        }
        xemu_vk_destroy_image(heap,images[i],image_allocations[i]);
        if(i%7==0)xemu_vk_trim_memory(heap);
    }
    for(unsigned i=0;i<2;i++)xemu_vk_destroy_buffer(heap,buffers[i],allocations[i]);
    xemu_vk_allocator_destroy(heap);assert(!errors);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);
    vkDestroyDevice(device,NULL);
    if(messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy=(PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
        destroy(instance,messenger,NULL);
    }
    vkDestroyInstance(instance,NULL);assert(!errors);
    printf("PASS: 64 real pooled image clears/direct mapped readbacks and offset buffer copies, exact pixels/guards and live-resource trimming; validation=%u\n",validation_enabled);
}
