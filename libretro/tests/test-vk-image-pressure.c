/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the deterministic Vulkan driver, not a real GPU or frontend. */
#define main memory_original_main
#include "test-vk-memory.c"
#undef main

#define PGRAPH_VK_SLOTS 3
typedef struct PGRAPHVkState {
    VkDevice device;
    XemuVkAllocator allocator;
    uint32_t settled_submit, slot_submit[PGRAPH_VK_SLOTS];
    VkFence slot_fence[PGRAPH_VK_SLOTS];
    bool slot_fence_pending[PGRAPH_VK_SLOTS], in_command_buffer;
    struct XemuVkTrashEntry {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkSampler sampler;
        VkFramebuffer framebuffer;
        VkImageView image_view;
        VkImage image;
        XemuVkAllocation allocation;
        VkImageCreateInfo image_ci;
        bool pool_ok, pool_view;
        uint32_t retire_after;
        uint64_t display_after, aux_after;
    } trash[3];
    int trash_n;
    bool keep_pool;
    VkImage pooled_image;
    XemuVkAllocation pooled_memory;
    struct { uint64_t settled; } display;
    uint64_t aux_settled;
} PGRAPHVkState;

static bool draw_ready, display_ready, aux_ready;
static unsigned fence_polls, pool_flushes;
static uint64_t display_serial, aux_serial;

static VKAPI_ATTR VkResult VKAPI_CALL fence_status(VkDevice device, VkFence fence)
{
    (void)device;
    assert(fence == HANDLE(VkFence, 1));
    fence_polls++;
    return draw_ready ? VK_SUCCESS : VK_NOT_READY;
}
PFN_vkGetFenceStatus vkGetFenceStatus = fence_status;
#define DESTROY(d,h,a) ((void)(d),(void)(h),(void)(a))
#define vkDestroyPipeline(d,h,a) DESTROY(d,h,a)
#define vkDestroyPipelineLayout(d,h,a) DESTROY(d,h,a)
#define vkDestroySampler(d,h,a) DESTROY(d,h,a)
#define vkDestroyFramebuffer(d,h,a) DESTROY(d,h,a)
#define vkDestroyImageView(d,h,a) DESTROY(d,h,a)

static void pgraph_vk_poll_display_retired(PGRAPHVkState *r)
{
    if (display_ready) r->display.settled = display_serial;
}
static void pgraph_vk_poll_aux_retired(PGRAPHVkState *r)
{
    if (aux_ready) r->aux_settled = aux_serial;
}
static bool pgraph_vk_image_pool_put(PGRAPHVkState *r, VkImage image,
                                   XemuVkAllocation allocation, const VkImageCreateInfo *ci)
{
    (void)ci;
    if (!r->keep_pool) return false;
    assert(!r->pooled_image);
    r->pooled_image = image;
    r->pooled_memory = allocation;
    return true;
}
static bool pgraph_vk_image_pool_put_with_view(PGRAPHVkState *r, VkImage image,
    XemuVkAllocation allocation, const VkImageCreateInfo *ci, VkImageView view)
{
    (void)view;
    return pgraph_vk_image_pool_put(r, image, allocation, ci);
}
static void pgraph_vk_image_pool_flush(PGRAPHVkState *r)
{
    if (r->pooled_image) {
        xemu_vk_destroy_image(r->allocator, r->pooled_image, r->pooled_memory);
        r->pooled_image = VK_NULL_HANDLE;
        r->pooled_memory = NULL;
    }
    pool_flushes++;
}
/* PRODUCTION */

int main(void)
{
    const unsigned orders[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    properties.memoryHeapCount = properties.memoryTypeCount = 1;
    properties.memoryHeaps[0].size = 8u << 20;
    properties.memoryTypes[0] = (VkMemoryType){VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0};
    requirement_bits = 1;
    unsigned cases = 0;
    for (unsigned pool = 0; pool < 2; pool++)
    for (unsigned wrap = 0; wrap < 2; wrap++) {
        for (unsigned open = 0; open < 2; open++) {
            for (unsigned order = 0; order < 6; order++) {
                PGRAPHVkState r = {.device = HANDLE(VkDevice, 1), .keep_pool = pool};
                assert(xemu_vk_allocator_create(HANDLE(VkPhysicalDevice, 1), r.device,
                                                false, &r.allocator) == VK_SUCCESS);
                assert(xemu_vk_allocator_reserve(r.allocator, 0, 0, 1u << 20) == VK_SUCCESS);
                unsigned backing = creates, mapping = maps, released = frees;
                VkImageCreateInfo ci = image_info(1u << 19);
                XemuVkMemoryRequest request = {0};
                VkImage live, retired, replacement;
                XemuVkAllocation live_mem, retired_mem, replacement_mem;
                assert(xemu_vk_create_image(r.allocator, &ci, &request, &live, &live_mem) == VK_SUCCESS);
                assert(xemu_vk_create_image(r.allocator, &ci, &request, &retired, &retired_mem) == VK_SUCCESS);
                Resource *resource = &images[number((uintptr_t)live)];
                unsigned memory = resource->memory;
                VkDeviceSize offset = resource->offset;
                memset(memories[memory].bytes + offset, 0xA7, (size_t)resource->size);
                r.settled_submit = wrap ? UINT32_MAX - 1 : 10;
                r.slot_submit[0] = r.settled_submit + 1;
                r.slot_fence_pending[0] = true;
                r.slot_fence[0] = HANDLE(VkFence, 1);
                r.in_command_buffer = open;
                r.trash[0] = (struct XemuVkTrashEntry){.image = retired, .allocation = retired_mem, .pool_ok = true,
                    .retire_after = r.slot_submit[0] + open, .display_after = 7, .aux_after = 9};
                r.trash_n = 1;
                draw_ready = display_ready = aux_ready = false;
                display_serial = 7; aux_serial = 9;
                pool_flushes = fence_polls = 0;
                xemu_vk_allocator_set_reclaim(r.allocator, reclaim_retired_images, &r);
                unsigned overflow=0;
                for (unsigned step = 0; step < 3; step++) {
                    switch (orders[order][step]) {
                    case 0: draw_ready = true; break;
                    case 1: display_ready = true; break;
                    case 2: aux_ready = true; break;
                    }
                    VkResult result = xemu_vk_create_image(r.allocator, &ci, &request,
                                                          &replacement, &replacement_mem);
                    if (step < 2 || open) {
                        assert(result == VK_SUCCESS && replacement && replacement_mem);
                        overflow++;
                        assert(r.trash_n == 1 && images[number((uintptr_t)retired)].live);
                    } else {
                        assert(result == VK_SUCCESS && !r.trash_n);
                    }
                    xemu_vk_destroy_image(r.allocator, replacement, replacement_mem);
                    assert(r.slot_fence_pending[0] && r.in_command_buffer == !!open);
                    assert(creates == backing+overflow && maps == mapping && frees == released+overflow);
                    for (unsigned i = 0; i < resource->size; i++) assert(memories[memory].bytes[offset + i] == 0xA7);
                }
                if (open) {
                    /* Only a subsequently submitted and completed recording can
                     * release an image referenced by the current recording. */
                    r.slot_submit[0]++;
                    assert(xemu_vk_create_image(r.allocator, &ci, &request,
                                               &replacement, &replacement_mem) == VK_SUCCESS);
                    assert(!r.trash_n && r.in_command_buffer && r.slot_fence_pending[0]);
                    xemu_vk_destroy_image(r.allocator, replacement, replacement_mem);
                }
                assert(pool_flushes && fence_polls);
                assert(creates == backing+overflow && maps == mapping && frees == released+overflow);
                xemu_vk_destroy_image(r.allocator, live, live_mem);
                xemu_vk_allocator_destroy(r.allocator);
                assert(!live_memory && !live_resources);
                cases++;
            }
        }
    }

    /* Driver-side creation failure happens before allocate() can reclaim. */
    PGRAPHVkState r = {.device = HANDLE(VkDevice, 1)};
    assert(xemu_vk_allocator_create(HANDLE(VkPhysicalDevice, 1), r.device, false, &r.allocator) == VK_SUCCESS);
    assert(xemu_vk_allocator_reserve(r.allocator, 0, 0, 1u << 20) == VK_SUCCESS);
    xemu_vk_allocator_set_reclaim(r.allocator, reclaim_retired_images, &r);
    unsigned backing = creates, mapping = maps, released = frees;
    const VkResult errors[] = {VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_HOST_MEMORY,
                              VK_ERROR_FORMAT_NOT_SUPPORTED};
    for (unsigned e = 0; e < 3; e++) for (unsigned failures = 1; failures <= 2; failures++) {
        image_create_error = errors[e]; image_create_failures = failures;
        unsigned attempts = image_create_attempts;
        pool_flushes = 0;
        VkImage image; XemuVkAllocation allocation;
        VkImageCreateInfo ci = image_info(4096);
        VkResult result = xemu_vk_create_image(r.allocator, &ci, &(XemuVkMemoryRequest){0}, &image, &allocation);
        assert(image_create_attempts - attempts == (e == 2 ? 1u : 2u));
        assert(pool_flushes == (e == 2 ? 0u : 1u));
        if (e != 2 && failures == 1) {
            assert(result == VK_SUCCESS && image && allocation);
            xemu_vk_destroy_image(r.allocator, image, allocation);
        } else {
            assert(result == errors[e] && !image && !allocation);
        }
        image_create_failures = 0;
        assert(creates == backing && maps == mapping && frees == released);
    }
    xemu_vk_allocator_destroy(r.allocator);
    assert(!live_memory && !live_resources);
    printf("PASS: %u pressure schedules; draw/display/aux and open-recording safety, serial wrapping, live bytes preserved; overflow only while resources remain pending, no extra mapping; six driver-creation failure/retry cases\n", cases);
}
