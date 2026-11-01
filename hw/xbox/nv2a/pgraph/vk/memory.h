/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef XEMU_VK_MEMORY_H
#define XEMU_VK_MEMORY_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

/* Renderer-owned, single-threaded heap. Resource retirement stays with the
 * renderer: destroying a resource means its GPU users have already finished.
 * Host buffers are mapped for their allocation's lifetime. Optimal images
 * and linear resources never share a block (bufferImageGranularity). */
typedef struct XemuVkHeap *XemuVkAllocator;
typedef struct XemuVkMemory *XemuVkAllocation;
typedef struct XemuVkMemoryRequest {
    bool host;
    bool dedicated;
} XemuVkMemoryRequest;
typedef struct XemuVkAllocationInfo {
    VkDeviceSize size;
    uint32_t memoryType;
} XemuVkAllocationInfo;
/* Cache-eviction working-set estimate: external driver usage plus our live
 * resource spans when sealed. Permanently reserved spare space is excluded. */
typedef struct XemuVkBudget {
    VkDeviceSize usage, budget;
} XemuVkBudget;

VkResult xemu_vk_allocator_create(VkPhysicalDevice physical, VkDevice device,
                                  bool memory_budget, XemuVkAllocator *out);
/* Startup only. After success, gameplay can only suballocate these blocks;
 * no driver allocation, mapping, trimming or metadata growth is permitted. */
VkResult xemu_vk_allocator_reserve(XemuVkAllocator allocator,
    VkDeviceSize host_bytes, VkDeviceSize device_bytes, VkDeviceSize image_bytes);
/* Owner-thread callback may release already-retired cached resources. It
 * must not allocate or wait for unfinished GPU work. */
void xemu_vk_allocator_set_reclaim(XemuVkAllocator allocator,
    void (*reclaim)(void *), void *opaque);
void xemu_vk_allocator_destroy(XemuVkAllocator allocator);
VkResult xemu_vk_create_buffer(XemuVkAllocator allocator,
    const VkBufferCreateInfo *ci, const XemuVkMemoryRequest *request,
    VkBuffer *buffer, XemuVkAllocation *allocation);
VkResult xemu_vk_create_image(XemuVkAllocator allocator,
    const VkImageCreateInfo *ci, const XemuVkMemoryRequest *request,
    VkImage *image, XemuVkAllocation *allocation);
void xemu_vk_destroy_buffer(XemuVkAllocator allocator, VkBuffer buffer,
                            XemuVkAllocation allocation);
void xemu_vk_destroy_image(XemuVkAllocator allocator, VkImage image,
                           XemuVkAllocation allocation);
VkResult xemu_vk_map_memory(XemuVkAllocator allocator,
                            XemuVkAllocation allocation, void **out);
VkResult xemu_vk_flush_allocation(XemuVkAllocator allocator,
    XemuVkAllocation allocation, VkDeviceSize offset, VkDeviceSize size);
VkResult xemu_vk_invalidate_allocation(XemuVkAllocator allocator,
    XemuVkAllocation allocation, VkDeviceSize offset, VkDeviceSize size);
void xemu_vk_get_allocation_memory_properties(XemuVkAllocator allocator,
    XemuVkAllocation allocation, VkMemoryPropertyFlags *out);
void xemu_vk_get_allocation_info(XemuVkAllocator allocator,
    XemuVkAllocation allocation, XemuVkAllocationInfo *out);
void xemu_vk_get_memory_properties(XemuVkAllocator allocator,
    const VkPhysicalDeviceMemoryProperties **out);
void xemu_vk_get_heap_budgets(XemuVkAllocator allocator, XemuVkBudget *out);
void xemu_vk_update_budget(XemuVkAllocator allocator, uint32_t frame);
/* Drops only empty blocks, never waits or relocates a live resource. */
void xemu_vk_trim_memory(XemuVkAllocator allocator);

#endif
