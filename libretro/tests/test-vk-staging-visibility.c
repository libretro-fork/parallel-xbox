/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
/* This fixture models VMA allocation handles as integer table indices. */
#undef VK_NULL_HANDLE
#define VK_NULL_HANDLE 0
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
enum {BUFFER_INDEX_STAGING,BUFFER_INDEX,BUFFER_VERTEX_INLINE_STAGING,BUFFER_VERTEX_INLINE,BUFFER_UNIFORM_STAGING,BUFFER_UNIFORM,BUFFER_STAGING_SRC,BUFFER_STAGING_DST};
typedef struct {VkBuffer buffer;VkBufferUsageFlags usage;int alloc_info;uintptr_t allocation;VkMemoryPropertyFlags properties;size_t buffer_offset,buffer_size;uint8_t *mapped;} StorageBuffer;
typedef struct {void *allocator;StorageBuffer storage_buffers[8];VkBuffer vram_buffer;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static VkMemoryPropertyFlags next_properties,properties[32];
static unsigned char host_bytes[32][64],visible_bytes[32][64],device_bytes[32][64];
static uintptr_t next_allocation;
static unsigned invalidates,flushes,copies,barriers,maps,unmaps,destroys,host_barriers;
static unsigned expected_dst;static size_t expected_size,expected_offset;
static VkResult vmaCreateBuffer(void *allocator,const VkBufferCreateInfo *info,const int *allocation_info,VkBuffer *buffer,uintptr_t *allocation,void *extra){(void)allocation_info;assert(allocator==(void*)(uintptr_t)7&&!extra&&info->size==64&&info->sharingMode==VK_SHARING_MODE_EXCLUSIVE);assert(next_allocation+1<32);*allocation=++next_allocation;*buffer=(VkBuffer)*allocation;properties[*allocation]=next_properties;return VK_SUCCESS;}
static void vmaGetAllocationMemoryProperties(void *allocator,uintptr_t allocation,VkMemoryPropertyFlags *out){assert(allocator==(void*)(uintptr_t)7&&allocation&&allocation<=next_allocation);*out=properties[allocation];}
static void vmaDestroyBuffer(void *allocator,VkBuffer buffer,uintptr_t allocation){assert(allocator==(void*)(uintptr_t)7&&(uintptr_t)buffer==allocation);destroys++;}
static void vmaUnmapMemory(void *allocator,uintptr_t allocation){assert(allocator==(void*)(uintptr_t)7&&allocation);unmaps++;}
static VkResult vmaMapMemory(void *allocator,uintptr_t allocation,void **out){assert(allocator==(void*)(uintptr_t)7&&allocation);*out=host_bytes[allocation];maps++;return VK_SUCCESS;}
static VkResult vmaFlushAllocation(void *allocator,uintptr_t allocation,VkDeviceSize offset,VkDeviceSize size){assert(allocator==(void*)(uintptr_t)7&&offset==expected_offset&&size==expected_size&&!(properties[allocation]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));memcpy(visible_bytes[allocation]+offset,host_bytes[allocation]+offset,size);flushes++;return VK_SUCCESS;}
static VkResult vmaInvalidateAllocation(void *allocator,uintptr_t allocation,VkDeviceSize offset,VkDeviceSize size){assert(allocator==(void*)(uintptr_t)7&&!offset&&size==expected_size&&!(properties[allocation]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));memcpy(host_bytes[allocation],device_bytes[allocation],size);invalidates++;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer cmd,VkBuffer src,VkBuffer dst,uint32_t count,const VkBufferCopy *regions){assert(cmd==(VkCommandBuffer)(uintptr_t)4&&count==1&&!regions[0].srcOffset&&!regions[0].dstOffset&&regions[0].size==expected_size);uintptr_t s=(uintptr_t)src,d=(uintptr_t)dst;const void *input=properties[s]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT?host_bytes[s]:visible_bytes[s];assert(!memcmp(input,host_bytes[s],expected_size));memcpy(device_bytes[d],input,expected_size);copies++;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier *memory,uint32_t count,const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images){if(src==VK_PIPELINE_STAGE_HOST_BIT){assert(cmd==(VkCommandBuffer)(uintptr_t)4&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&!flags&&!memory_count&&!memory&&count==1&&!image_count&&!images);assert(buffers[0].srcAccessMask==VK_ACCESS_HOST_WRITE_BIT&&buffers[0].dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&buffers[0].buffer==(VkBuffer)(uintptr_t)0x555&&buffers[0].size==VK_WHOLE_SIZE&&!buffers[0].offset);host_barriers++;return;}assert(cmd==(VkCommandBuffer)(uintptr_t)4&&src==VK_PIPELINE_STAGE_TRANSFER_BIT&&!flags&&!memory_count&&!memory&&count==1&&!image_count&&!images);VkAccessFlags access=expected_dst==BUFFER_INDEX?VK_ACCESS_INDEX_READ_BIT:expected_dst==BUFFER_VERTEX_INLINE?VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:VK_ACCESS_UNIFORM_READ_BIT;VkPipelineStageFlags stages=expected_dst==BUFFER_UNIFORM?(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT):VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;assert(dst==stages&&buffers[0].srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&buffers[0].dstAccessMask==access&&buffers[0].size==expected_size&&!buffers[0].offset&&buffers[0].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&buffers[0].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);barriers++;}
/* PRODUCTION */
int main(void){quiet_test_errors();unsigned cases=0;
 for(unsigned mask=0;mask<8;mask++){
  PGRAPHVkState r={.allocator=(void*)(uintptr_t)7};PGRAPHState pg={&r};next_allocation=0;expected_offset=0;flushes=copies=barriers=maps=unmaps=destroys=host_barriers=0;memset(visible_bytes,0xa5,sizeof(visible_bytes));
  for(unsigned i=0;i<6;i++){StorageBuffer *b=&r.storage_buffers[i];b->buffer_size=64;b->usage=i%2?VK_BUFFER_USAGE_TRANSFER_DST_BIT:VK_BUFFER_USAGE_TRANSFER_SRC_BIT;next_properties=i%2?VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|((mask&(1u<<(i/2)))?VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0);b->properties=~next_properties;create_buffer(&pg,b);assert(b->properties==next_properties);if(!(i%2)){assert(vmaMapMemory(r.allocator,b->allocation,(void**)&b->mapped)==VK_SUCCESS);}}
  for(unsigned j=0;j<3;j++){
   unsigned src=2*j,dst=src+1;StorageBuffer *s=&r.storage_buffers[src],*d=&r.storage_buffers[dst];expected_size=11+j*13;expected_dst=dst;
   for(unsigned b=0;b<expected_size;b++)s->mapped[b]=(unsigned char)(mask*17+j*31+b);s->buffer_offset=expected_size;
   unsigned before=flushes;sync_staging_buffer(&pg,(VkCommandBuffer)(uintptr_t)4,src,dst);assert(!s->buffer_offset&&flushes==before+!(s->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&!memcmp(device_bytes[d->allocation],s->mapped,expected_size));unsigned old_copies=copies,old_barriers=barriers;sync_staging_buffer(&pg,(VkCommandBuffer)(uintptr_t)4,src,dst);assert(copies==old_copies&&barriers==old_barriers);cases++;
  }
  /* Growth can change the selected memory type: cached properties must follow. */
  StorageBuffer *s=&r.storage_buffers[BUFFER_UNIFORM_STAGING];next_properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|((s->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?0:VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);recreate_buffer(&pg,s,64);assert(s->properties==next_properties&&maps==4&&unmaps==1&&destroys==1&&s->mapped==host_bytes[s->allocation]);expected_size=32;expected_dst=BUFFER_UNIFORM;memset(s->mapped,0x5a,32);s->buffer_offset=32;unsigned before=flushes;sync_staging_buffer(&pg,(VkCommandBuffer)(uintptr_t)4,BUFFER_UNIFORM_STAGING,BUFFER_UNIFORM);assert(flushes==before+!(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));cases++;
  StorageBuffer *ring=&r.storage_buffers[BUFFER_STAGING_SRC];ring->buffer_size=64;ring->usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;next_properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|((mask&1)?VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0);create_buffer(&pg,ring);assert(vmaMapMemory(r.allocator,ring->allocation,(void**)&ring->mapped)==VK_SUCCESS);expected_offset=16;expected_size=17;memset(ring->mapped+16,0x37,17);before=flushes;pgraph_vk_staging_flush(&pg,16,17);assert(flushes==before+!(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));before=flushes;pgraph_vk_staging_flush(&pg,16,0);assert(flushes==before);cases++;
  StorageBuffer *readback=&r.storage_buffers[BUFFER_STAGING_DST];readback->buffer_size=64;readback->usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;create_buffer(&pg,readback);assert(vmaMapMemory(r.allocator,readback->allocation,(void**)&readback->mapped)==VK_SUCCESS);
  for(unsigned generation=0;generation<2;generation++){
   if(generation){next_properties^=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;recreate_buffer(&pg,readback,64);}
   expected_size=37;memset(readback->mapped,0xa5,64);memset(device_bytes[readback->allocation],0x63,37);
   if(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)memcpy(readback->mapped,device_bytes[readback->allocation],37);
   unsigned old=invalidates;invalidate_readback(&r,37);assert(invalidates==old+!(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
   for(unsigned b=0;b<64;b++)assert(readback->mapped[b]==(b<37?0x63:0xa5));
   old=invalidates;invalidate_readback(&r,0);assert(invalidates==old);cases++;
  }
  pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,false);assert(!host_barriers);r.vram_buffer=(VkBuffer)(uintptr_t)0x555;pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,false);assert(!host_barriers);pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,true);assert(host_barriers==1);
 }
 printf("Staging visibility: %u coherent/non-coherent copy and recreation cases passed; flush-before-copy, vertex/fragment UBO barriers, empty skips, payload bytes, readback invalidation/recreation and imported-RAM-only upload barriers verified\n",cases);return 0;}
