/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
/* Allocation handles are modeled as integer table indices. */
#undef VK_NULL_HANDLE
#define VK_NULL_HANDLE 0
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {int staging,device;} StagedBuffer;
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
enum {BUFFER_INDEX_STAGING,BUFFER_INDEX,BUFFER_VERTEX_INLINE_STAGING,BUFFER_VERTEX_INLINE,BUFFER_UNIFORM_STAGING,BUFFER_UNIFORM,BUFFER_STAGING_SRC,BUFFER_STAGING_DST,BUFFER_UNSWIZZLE_OUT};
typedef struct {VkBuffer buffer;VkBufferUsageFlags usage;int alloc_info;uintptr_t allocation;VkMemoryPropertyFlags properties;size_t buffer_offset,buffer_size;uint8_t *mapped;} StorageBuffer;
typedef struct {void *allocator;StorageBuffer storage_buffers[9];VkBuffer vram_buffer;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static VkMemoryPropertyFlags next_properties,properties[32];
static unsigned char host_bytes[32][64],visible_bytes[32][64],device_bytes[32][64];
static uintptr_t next_allocation;
static unsigned invalidates,flushes,copies,barriers,maps,unmaps,destroys,host_barriers;
static unsigned unswizzle_barriers;
static VkDeviceSize source_first,source_size,palette_extra;
static const unsigned texel_counts[]={1,8,64,512,2048};
static unsigned batch_mask,batch_copy_base;
static unsigned expected_dst;static size_t expected_size,expected_offset;
static VkResult xemu_vk_create_buffer(void *allocator,const VkBufferCreateInfo *info,const int *allocation_info,VkBuffer *buffer,uintptr_t *allocation){(void)allocation_info;assert(allocator==(void*)(uintptr_t)7&&info->size==64&&info->sharingMode==VK_SHARING_MODE_EXCLUSIVE);assert(next_allocation+1<32);*allocation=++next_allocation;*buffer=(VkBuffer)*allocation;properties[*allocation]=next_properties;return VK_SUCCESS;}
static void xemu_vk_get_allocation_memory_properties(void *allocator,uintptr_t allocation,VkMemoryPropertyFlags *out){assert(allocator==(void*)(uintptr_t)7&&allocation&&allocation<=next_allocation);*out=properties[allocation];}
static void xemu_vk_destroy_buffer(void *allocator,VkBuffer buffer,uintptr_t allocation){assert(allocator==(void*)(uintptr_t)7&&(uintptr_t)buffer==allocation);destroys++;}
static VkResult xemu_vk_map_memory(void *allocator,uintptr_t allocation,void **out){assert(allocator==(void*)(uintptr_t)7&&allocation);*out=host_bytes[allocation];maps++;return VK_SUCCESS;}
static VkResult xemu_vk_flush_allocation(void *allocator,uintptr_t allocation,VkDeviceSize offset,VkDeviceSize size){assert(allocator==(void*)(uintptr_t)7&&offset==expected_offset&&size==expected_size&&!(properties[allocation]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));memcpy(visible_bytes[allocation]+offset,host_bytes[allocation]+offset,size);flushes++;return VK_SUCCESS;}
static VkResult xemu_vk_invalidate_allocation(void *allocator,uintptr_t allocation,VkDeviceSize offset,VkDeviceSize size){assert(allocator==(void*)(uintptr_t)7&&!offset&&size==expected_size&&!(properties[allocation]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));memcpy(host_bytes[allocation],device_bytes[allocation],size);invalidates++;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer cmd,VkBuffer src,VkBuffer dst,uint32_t count,const VkBufferCopy *regions){assert(cmd==(VkCommandBuffer)(uintptr_t)4&&count==1&&!regions[0].srcOffset&&!regions[0].dstOffset&&regions[0].size==expected_size);uintptr_t s=(uintptr_t)src,d=(uintptr_t)dst;const void *input=properties[s]&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT?host_bytes[s]:visible_bytes[s];assert(!memcmp(input,host_bytes[s],expected_size));memcpy(device_bytes[d],input,expected_size);copies++;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier *memory,uint32_t count,const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images){if(dst==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT){
 assert(src==(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT));
 assert(cmd==(VkCommandBuffer)(uintptr_t)4&&!flags&&!memory_count&&!memory&&count==(palette_extra?3u:2u)&&!image_count&&!images);
 assert(buffers[0].buffer==(VkBuffer)(uintptr_t)0x555&&buffers[0].size==source_size&&buffers[0].offset==source_first);
 assert(buffers[0].srcAccessMask==(VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT)&&buffers[0].dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
 assert(buffers[1].buffer==(VkBuffer)(uintptr_t)0x556&&buffers[1].offset==16&&buffers[1].size==17);
 assert(buffers[1].srcAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&buffers[1].dstAccessMask==VK_ACCESS_SHADER_WRITE_BIT);
 if(palette_extra){assert(buffers[2].buffer==buffers[0].buffer&&buffers[2].offset==palette_extra&&buffers[2].size==1024);assert(buffers[2].srcAccessMask==buffers[0].srcAccessMask&&buffers[2].dstAccessMask==buffers[0].dstAccessMask);}
 for(unsigned i=0;i<count;i++)assert(buffers[i].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&buffers[i].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
 unswizzle_barriers++;return;
}if(src==(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)){assert(cmd==(VkCommandBuffer)(uintptr_t)4&&dst==VK_PIPELINE_STAGE_TRANSFER_BIT&&!flags&&!memory_count&&!memory&&count==1&&!image_count&&!images);assert(buffers[0].srcAccessMask==(VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT)&&buffers[0].dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&buffers[0].buffer==(VkBuffer)(uintptr_t)0x555&&buffers[0].size==17&&buffers[0].offset==16);host_barriers++;return;}if(batch_mask){
 unsigned n=0;VkPipelineStageFlags stages=0;
 assert(cmd==(VkCommandBuffer)(uintptr_t)4&&src==VK_PIPELINE_STAGE_TRANSFER_BIT&&!flags&&!memory_count&&!memory&&!image_count&&!images);
 for(unsigned j=0;j<3;j++)if(batch_mask&(1u<<j)){
  const VkBufferMemoryBarrier *b=&buffers[n++];
  assert(b->buffer==(VkBuffer)(uintptr_t)(2*j+2)&&!b->offset&&b->size==expected_size);
  assert(b->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&b->dstAccessMask==(j==0?VK_ACCESS_INDEX_READ_BIT:j==1?VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:VK_ACCESS_UNIFORM_READ_BIT));
  assert(b->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&b->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
  stages|=j==2?VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
 }
 assert(count==n&&dst==stages&&copies==batch_copy_base+n);barriers++;return;
}assert(cmd==(VkCommandBuffer)(uintptr_t)4&&src==VK_PIPELINE_STAGE_TRANSFER_BIT&&!flags&&!memory_count&&!memory&&count==1&&!image_count&&!images);VkAccessFlags access=expected_dst==BUFFER_INDEX?VK_ACCESS_INDEX_READ_BIT:expected_dst==BUFFER_VERTEX_INLINE?VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:VK_ACCESS_UNIFORM_READ_BIT;VkPipelineStageFlags stages=expected_dst==BUFFER_UNIFORM?(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT):VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;assert(dst==stages&&buffers[0].srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&buffers[0].dstAccessMask==access&&buffers[0].size==expected_size&&!buffers[0].offset&&buffers[0].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&buffers[0].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);barriers++;}
/* PRODUCTION */
int main(void){quiet_test_errors();unsigned cases=0,batch_cases=0;
 for(unsigned mask=0;mask<8;mask++){
  PGRAPHVkState r={.allocator=(void*)(uintptr_t)7};PGRAPHState pg={&r};next_allocation=0;expected_offset=0;flushes=copies=barriers=maps=unmaps=destroys=host_barriers=0;memset(visible_bytes,0xa5,sizeof(visible_bytes));
  for(unsigned i=0;i<6;i++){StorageBuffer *b=&r.storage_buffers[i];b->buffer_size=64;b->usage=i%2?VK_BUFFER_USAGE_TRANSFER_DST_BIT:VK_BUFFER_USAGE_TRANSFER_SRC_BIT;next_properties=i%2?VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|((mask&(1u<<(i/2)))?VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0);b->properties=~next_properties;create_buffer(&pg,b);assert(b->properties==next_properties);if(!(i%2)){assert(xemu_vk_map_memory(r.allocator,b->allocation,(void**)&b->mapped)==VK_SUCCESS);}}
  for(unsigned j=0;j<3;j++){
   unsigned src=2*j,dst=src+1;StorageBuffer *s=&r.storage_buffers[src],*d=&r.storage_buffers[dst];expected_size=11+j*13;expected_dst=dst;
   for(unsigned b=0;b<expected_size;b++)s->mapped[b]=(unsigned char)(mask*17+j*31+b);s->buffer_offset=expected_size;
   unsigned before=flushes;sync_staging_buffers(&pg,(VkCommandBuffer)(uintptr_t)4);assert(!s->buffer_offset&&flushes==before+!(s->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&!memcmp(device_bytes[d->allocation],s->mapped,expected_size));unsigned old_copies=copies,old_barriers=barriers;sync_staging_buffers(&pg,(VkCommandBuffer)(uintptr_t)4);assert(copies==old_copies&&barriers==old_barriers);cases++;
  }
  for(unsigned active=0;active<8;active++){
   expected_size=32;batch_mask=active;batch_copy_base=copies;
   unsigned old_barriers=barriers,old_flushes=flushes,n=0,needed_flushes=0;
   for(unsigned j=0;j<3;j++){
    StorageBuffer *s=&r.storage_buffers[2*j];s->buffer_offset=(active&(1u<<j))?32:0;
    memset(s->mapped,0x30+active+j,32);
    if(s->buffer_offset){n++;needed_flushes+=!(s->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);}
   }
   sync_staging_buffers(&pg,(VkCommandBuffer)(uintptr_t)4);
   assert(copies==batch_copy_base+n&&barriers==old_barriers+!!active&&flushes==old_flushes+needed_flushes);
   for(unsigned j=0;j<3;j++){
    StorageBuffer *s=&r.storage_buffers[2*j],*d=&r.storage_buffers[2*j+1];
    assert(!s->buffer_offset);
    if(active&(1u<<j))assert(!memcmp(device_bytes[d->allocation],s->mapped,32));
   }
   batch_cases++;
  }
  batch_mask=0;
  /* Growth can change the selected memory type: cached properties must follow. */
  StorageBuffer *s=&r.storage_buffers[BUFFER_UNIFORM_STAGING];next_properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|((s->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?0:VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);recreate_buffer(&pg,s,64);assert(s->properties==next_properties&&maps==4&&unmaps==0&&destroys==1&&s->mapped==host_bytes[s->allocation]);expected_size=32;expected_dst=BUFFER_UNIFORM;memset(s->mapped,0x5a,32);s->buffer_offset=32;unsigned before=flushes;sync_staging_buffers(&pg,(VkCommandBuffer)(uintptr_t)4);assert(flushes==before+!(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));cases++;
  StorageBuffer *ring=&r.storage_buffers[BUFFER_STAGING_SRC];ring->buffer_size=64;ring->usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;next_properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|((mask&1)?VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0);create_buffer(&pg,ring);assert(xemu_vk_map_memory(r.allocator,ring->allocation,(void**)&ring->mapped)==VK_SUCCESS);expected_offset=16;expected_size=17;memset(ring->mapped+16,0x37,17);before=flushes;pgraph_vk_staging_flush(&pg,16,17);assert(flushes==before+!(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));before=flushes;pgraph_vk_staging_flush(&pg,16,0);assert(flushes==before);cases++;
  StorageBuffer *readback=&r.storage_buffers[BUFFER_STAGING_DST];readback->buffer_size=64;readback->usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;create_buffer(&pg,readback);assert(xemu_vk_map_memory(r.allocator,readback->allocation,(void**)&readback->mapped)==VK_SUCCESS);
  for(unsigned generation=0;generation<2;generation++){
   if(generation){next_properties^=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;recreate_buffer(&pg,readback,64);}
   expected_size=37;memset(readback->mapped,0xa5,64);memset(device_bytes[readback->allocation],0x63,37);
   if(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)memcpy(readback->mapped,device_bytes[readback->allocation],37);
   unsigned old=invalidates;invalidate_readback(&r,37);assert(invalidates==old+!(next_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
   for(unsigned b=0;b<64;b++)assert(readback->mapped[b]==(b<37?0x63:0xa5));
   old=invalidates;invalidate_readback(&r,0);assert(invalidates==old);cases++;
  }
  pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,false,16,17);assert(!host_barriers);r.vram_buffer=(VkBuffer)(uintptr_t)0x555;pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,false,16,17);assert(!host_barriers);pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,true,16,17);assert(host_barriers==1);pgraph_vk_upload_source_barrier(&pg,(VkCommandBuffer)(uintptr_t)4,true,16,0);assert(host_barriers==1);
  r.storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer=(VkBuffer)(uintptr_t)0x556;
  for(unsigned src=0;src<8;src++)for(unsigned bpp=1;bpp<=4;bpp*=2)
  for(unsigned ni=0;ni<5;ni++)for(unsigned pal=0;pal<3;pal++){
   unsigned n=texel_counts[ni];
   if(src%bpp||(pal&&bpp!=1))continue;
   unsigned palette=pal==0?0:pal==1?16:4096;
   uint64_t low=UINT64_MAX,high=0;
   for(unsigned byte=0;byte<n*bpp;byte++){uint64_t word=(src+byte)/4;if(word<low)low=word;if(word>high)high=word;}
   source_first=low*4;source_size=(high-low+1)*4;
   palette_extra=palette&&(palette<source_first||palette+1024>source_first+source_size)?palette:0;
   unsigned prior_unswizzle=unswizzle_barriers;
   unswizzle_source_barriers(&r,(VkCommandBuffer)(uintptr_t)4,16,17,src,n,bpp,palette);
   assert(unswizzle_barriers==prior_unswizzle+1);
  }
 }
 printf("Staging batches: %u active-buffer/coherency combinations; all copies before one exact-range visibility batch, empty batches skipped\n",batch_cases);
 printf("Unswizzle visibility: %u source/ring dependency schedules passed\n",unswizzle_barriers);
 printf("Staging visibility: %u coherent/non-coherent copy and recreation cases passed; flush-before-copy, vertex/fragment UBO barriers, empty skips, payload bytes, readback invalidation/recreation and imported-RAM-only upload barriers verified\n",cases);return 0;}
