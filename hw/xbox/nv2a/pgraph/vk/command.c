/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "renderer.h"
#include "libretro_shared.h"
#include "ui/xemu-settings.h"

static void create_command_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);

    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = indices.queue_family,
    };
    VK_CHECK(
        vkCreateCommandPool(r->device, &create_info, NULL, &r->command_pool));
}

static void destroy_command_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyCommandPool(r->device, r->command_pool, NULL);
}

static void create_command_buffers(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = ARRAY_SIZE(r->command_buffers),
    };
    VK_CHECK(
        vkAllocateCommandBuffers(r->device, &alloc_info, r->command_buffers));

    /* [0..N) = main slots; [N..2N) = per-slot staging-sync; [2N..) = the
     * aux ring for single-time commands (a fence each). */
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        r->slot_main[s] = r->command_buffers[s];
        r->slot_sync[s] = r->command_buffers[PGRAPH_VK_SLOTS + s];
    }
    r->slot = 0;
    r->command_buffer = r->slot_main[0];
    for (int s = 0; s < PGRAPH_VK_AUX_SLOTS; s++) {
        r->aux[s].cmd = r->command_buffers[2 * PGRAPH_VK_SLOTS + s];
    }
    r->aux_slot = 0;
    r->aux_oldest = 0;
    r->aux_npending = 0;
    for (int i = 0; i < PGRAPH_VK_RINGS; i++) {
        r->ring_head[i] = 0;
    }
}

static void destroy_command_buffers(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkFreeCommandBuffers(r->device, r->command_pool,
                         ARRAY_SIZE(r->command_buffers), r->command_buffers);

    r->command_buffer = VK_NULL_HANDLE;
    for (int s = 0; s < PGRAPH_VK_AUX_SLOTS; s++) {
        r->aux[s].cmd = VK_NULL_HANDLE;
    }
}

/*
 * Every submission to the renderer's queue. In the libretro core the queue
 * can be the frontend's (libretro_shared.h), which is then only used under
 * the frontend's lock; where it is the renderer's own, the first batch
 * waits for the last frame drawn on the frontend's queue (display.c
 * submit_frame) before anything here draws to the surface it sampled.
 */
void pgraph_vk_queue_submit(PGRAPHVkState *r, uint32_t count,
                            const VkSubmitInfo *submits, VkFence fence)
{
    VkSubmitInfo with_wait[4];
    VkSemaphore waits[2];
    VkPipelineStageFlags wait_stages[2];

    if (r->display.shown_pending != VK_NULL_HANDLE && count >= 1 &&
        count <= ARRAY_SIZE(with_wait)) {
        memcpy(with_wait, submits, count * sizeof(*submits));
        assert(submits[0].waitSemaphoreCount <= 1);
        waits[0] = r->display.shown_pending;
        wait_stages[0] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (submits[0].waitSemaphoreCount) {
            waits[1] = submits[0].pWaitSemaphores[0];
            wait_stages[1] = submits[0].pWaitDstStageMask[0];
        }
        with_wait[0].waitSemaphoreCount = submits[0].waitSemaphoreCount + 1;
        with_wait[0].pWaitSemaphores = waits;
        with_wait[0].pWaitDstStageMask = wait_stages;
        submits = with_wait;
        r->display.shown_pending = VK_NULL_HANDLE;
    }
    xemu_libretro_vk_lock_queue();
    VK_CHECK(vkQueueSubmit(r->queue, count, submits, fence));
    xemu_libretro_vk_unlock_queue();
}

/*
 * Submit and wait for the work to finish: on a fence, not vkQueueWaitIdle,
 * which may not run on a queue the frontend is using and would also wait
 * for the frontend's own work.
 */
void pgraph_vk_queue_submit_and_wait(PGRAPHVkState *r, uint32_t count,
                                     const VkSubmitInfo *submits)
{
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(r->device, &fence_info, NULL, &fence));
    pgraph_vk_queue_submit(r, count, submits, fence);
    VK_CHECK(vkWaitForFences(r->device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(r->device, fence, NULL);
}

/* The oldest pending aux submission, retired: its fence waited for, or
 * only looked at when `poll` */
static bool aux_retire_oldest(PGRAPHVkState *r, bool poll)
{
    struct PGRAPHVkAuxSlot *a = &r->aux[r->aux_oldest];

    assert(r->aux_npending > 0 && a->pending);
    if (poll) {
        if (vkGetFenceStatus(r->device, a->fence) != VK_SUCCESS) {
            return false;
        }
    } else {
        VK_CHECK(vkWaitForFences(r->device, 1, &a->fence, VK_TRUE,
                                 UINT64_MAX));
    }
    VK_CHECK(vkResetFences(r->device, 1, &a->fence));
    a->pending = false;
    r->aux_oldest = (r->aux_oldest + 1) % PGRAPH_VK_AUX_SLOTS;
    r->aux_npending--;
    return true;
}

/* Wait for every aux submission: the command buffers and the staging ring
 * are free. For a buffer's recreation and for shutdown; nothing per frame
 * needs it. */
void pgraph_vk_wait_for_aux(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    while (r->aux_npending) {
        aux_retire_oldest(r, false);
    }
    if (!r->in_aux_command_buffer) {
        for (int i = 0; i < PGRAPH_VK_RINGS; i++) {
            r->ring_head[i] = 0;
        }
    }
}

VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_aux_command_buffer);
    struct PGRAPHVkAuxSlot *a = &r->aux[r->aux_slot];
    /* The slot's last submission, PGRAPH_VK_AUX_SLOTS ago: normally long
     * retired; a wait here means the GPU is that far behind the uploads. */
    while (a->pending) {
        aux_retire_oldest(r, false);
    }
    r->in_aux_command_buffer = true;
    for (int i = 0; i < PGRAPH_VK_RINGS; i++) {
        a->ring_start[i] = r->ring_head[i];
        a->ring_end[i] = r->ring_head[i];
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(a->cmd, &begin_info));

    return a->cmd;
}

static const int ring_buffer[PGRAPH_VK_RINGS] = {
    [PGRAPH_VK_RING_STAGING] = BUFFER_STAGING_SRC,
    [PGRAPH_VK_RING_UNSWIZZLE] = BUFFER_UNSWIZZLE_OUT,
};

/* The first byte of a ring still in use: by the oldest submission in
 * flight that uses any, else by the open recording. False when nothing of
 * the ring is in use. */
static bool ring_tail(PGRAPHVkState *r, int ring, size_t *tail)
{
    for (int i = 0; i < r->aux_npending; i++) {
        struct PGRAPHVkAuxSlot *a =
            &r->aux[(r->aux_oldest + i) % PGRAPH_VK_AUX_SLOTS];
        if (a->ring_end[ring] != a->ring_start[ring]) {
            *tail = a->ring_start[ring];
            return true;
        }
    }
    struct PGRAPHVkAuxSlot *a = &r->aux[r->aux_slot];
    if (a->ring_end[ring] != a->ring_start[ring]) {
        *tail = a->ring_start[ring];
        return true;
    }
    return false;
}

bool pgraph_vk_ring_alloc(PGRAPHState *pg, int ring, size_t size,
                          size_t alignment, VkDeviceSize *offset)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[ring_buffer[ring]];
    struct PGRAPHVkAuxSlot *a = &r->aux[r->aux_slot];

    assert(r->in_aux_command_buffer);
    assert(size > 0);
    if (size >= b->buffer_size) {
        if (ring != PGRAPH_VK_RING_STAGING) {
            return false;
        }
        /* Grown for it, once everything has settled: the recording so far
         * reads nothing of the ring, or the caller would not need more
         * than all of it */
        assert(a->ring_start[ring] == a->ring_end[ring]);
        pgraph_vk_buffer_ensure_capacity(pg, BUFFER_STAGING_SRC, 2 * size);
        b = &r->storage_buffers[BUFFER_STAGING_SRC];
        a->ring_start[ring] = a->ring_end[ring] = r->ring_head[ring] = 0;
    }

    for (;;) {
        /* pending submissions retired as their fences signal, no waiting */
        while (r->aux_npending && aux_retire_oldest(r, true)) {
        }
        size_t head = ROUND_UP(r->ring_head[ring], alignment);
        size_t tail;
        bool in_use = ring_tail(r, ring, &tail);

        if (!in_use) {
            /* empty: at the head if it fits there, else from the start */
            if (head + size > b->buffer_size) {
                head = 0;
            }
            a->ring_start[ring] = head;
        } else if (tail > r->ring_head[ring]) {
            /* the free bytes are [head, tail) */
            if (head + size > tail) {
                head = (size_t)-1;
            }
        } else {
            /* in use behind the head: [head, size) and [0, tail) are free
             * (tail == head is the ring full) */
            if (tail == r->ring_head[ring] || head + size > b->buffer_size) {
                head = size <= tail && tail != r->ring_head[ring] ?
                           0 : (size_t)-1;
            }
        }
        if (head != (size_t)-1) {
            r->ring_head[ring] = head + size;
            a->ring_end[ring] = r->ring_head[ring];
            *offset = head;
            return true;
        }
        if (r->aux_npending) {
            /* full: the oldest submission's bytes come back once it has
             * run - the one wait on this path */
            aux_retire_oldest(r, false);
            continue;
        }
        /* the open recording alone holds more than the ring */
        if (ring != PGRAPH_VK_RING_STAGING) {
            return false;
        }
        assert(!"staging ring: the open recording does not fit");
    }
}

VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg, size_t size,
                                     size_t alignment, uint8_t **ptr)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkDeviceSize offset = 0;

    bool ok = pgraph_vk_ring_alloc(pg, PGRAPH_VK_RING_STAGING, size,
                                   alignment, &offset);
    assert(ok);
    StorageBuffer *b = &r->storage_buffers[BUFFER_STAGING_SRC];
    assert(b->mapped);
    *ptr = b->mapped + offset;
    return offset;
}

void pgraph_vk_staging_flush(PGRAPHState *pg, VkDeviceSize offset,
                             size_t size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vmaFlushAllocation(r->allocator,
                       r->storage_buffers[BUFFER_STAGING_SRC].allocation,
                       offset, size);
}

/* Submit without blocking: the work runs ahead of the open recording, and
 * the barrier each caller records last (a layout transition) orders later
 * commands after it. Callers that read results back use the sync variant. */
void pgraph_vk_end_single_time_commands_async(PGRAPHState *pg,
                                              VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    struct PGRAPHVkAuxSlot *a = &r->aux[r->aux_slot];

    assert(r->in_aux_command_buffer && cmd == a->cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    /* through the core's wrapper: the queue is RetroArch's, shared */
    pgraph_vk_queue_submit(r, 1, &submit_info, a->fence);
    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_AUX);
    a->pending = true;
    if (!r->aux_npending) {
        r->aux_oldest = r->aux_slot;
    }
    r->aux_npending++;
    r->aux_slot = (r->aux_slot + 1) % PGRAPH_VK_AUX_SLOTS;
    r->in_aux_command_buffer = false;
}

/* Submit and wait for this submission: for a readback */
void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    int slot = r->aux_slot;

    pgraph_vk_end_single_time_commands_async(pg, cmd);
    while (r->aux[slot].pending) {
        aux_retire_oldest(r, false);
    }
}

/* Timestamps are defined only on a queue family with valid bits. */
static bool queue_has_timestamps(PGRAPHVkState *r)
{
    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &n, NULL);
    g_autofree VkQueueFamilyProperties *props =
        g_new(VkQueueFamilyProperties, n);
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &n, props);
    return indices.queue_family >= 0 && (uint32_t)indices.queue_family < n &&
           props[indices.queue_family].timestampValidBits > 0;
}

void pgraph_vk_init_command_buffers(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    create_command_pool(pg);
    create_command_buffers(pg);

    /* Frame GPU timestamps (draw.c pgraph_vk_frame_gpu_*), host-reset. */
    r->frame_gpu.pool = VK_NULL_HANDLE;
    r->frame_gpu.w = 0;
    r->frame_gpu.resolved = 0;
    memset(r->frame_gpu.w_end, 0, sizeof(r->frame_gpu.w_end));
    r->frame_gpu.busy_us = 0;
    if (r->host_query_reset && queue_has_timestamps(r)) {
        r->frame_gpu.capacity = 256;
        VkQueryPoolCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 2 * r->frame_gpu.capacity,
        };
        VK_CHECK(vkCreateQueryPool(r->device, &ci, NULL, &r->frame_gpu.pool));
        vkResetQueryPool(r->device, r->frame_gpu.pool, 0,
                         2 * r->frame_gpu.capacity);
        r->frame_gpu.scratch =
            g_malloc0(2 * r->frame_gpu.capacity * sizeof(uint64_t));
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    for (int s = 0; s < PGRAPH_VK_AUX_SLOTS; s++) {
        VK_CHECK(vkCreateFence(r->device, &fence_info, NULL,
                               &r->aux[s].fence));
        r->aux[s].pending = false;
    }

    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        VK_CHECK(vkCreateFence(r->device, &fence_info, NULL,
                               &r->slot_fence[s]));
        r->slot_fence_pending[s] = false;
        r->slot_submit[s] = 0;
        VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL,
                                   &r->slot_sem[s]));
    }
}

void pgraph_vk_finalize_command_buffers(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    pgraph_vk_wait_all_slots(r);
    pgraph_vk_wait_for_aux(pg);
    pgraph_vk_trash_drain(r);
    g_free(r->trash);
    r->trash = NULL;
    r->trash_cap = 0;
    for (int s = 0; s < PGRAPH_VK_AUX_SLOTS; s++) {
        vkDestroyFence(r->device, r->aux[s].fence, NULL);
    }
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        vkDestroyFence(r->device, r->slot_fence[s], NULL);
        vkDestroySemaphore(r->device, r->slot_sem[s], NULL);
    }
    destroy_command_buffers(pg);
    if (r->frame_gpu.pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(r->device, r->frame_gpu.pool, NULL);
        r->frame_gpu.pool = VK_NULL_HANDLE;
        g_free(r->frame_gpu.scratch);
        r->frame_gpu.scratch = NULL;
    }
    destroy_command_pool(pg);
}
