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

#include "ui/xemu-settings.h"
#include "renderer.h"
#include "libretro/dsp-ownership.h"

const StagedBuffer pgraph_vk_staged_buffers[3] = {
    { BUFFER_INDEX_STAGING, BUFFER_INDEX },
    { BUFFER_VERTEX_INLINE_STAGING, BUFFER_VERTEX_INLINE },
    { BUFFER_UNIFORM_STAGING, BUFFER_UNIFORM },
};

static void create_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer->buffer_size,
        .usage = buffer->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = xemu_vk_create_buffer(r->allocator, &buffer_create_info,
                                      &buffer->alloc_info, &buffer->buffer,
                                      &buffer->allocation);
    if (result != VK_SUCCESS) {
        /* which one, for a GPU that runs out of memory at start */
        fprintf(stderr, "nv2a: Vulkan buffer of %llu bytes (usage %x) failed: %d\n",
                (unsigned long long)buffer->buffer_size, buffer->usage, result);
    }
    VK_CHECK(result);
    xemu_vk_get_allocation_memory_properties(r->allocator, buffer->allocation,
                                     &buffer->properties);
}

static void destroy_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    xemu_vk_destroy_buffer(r->allocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;
}

/* Reserve conversion capacity at initialization, including the largest scale
 * accepted without a renderer restart. No on-demand compute-buffer growth. */
static VkDeviceSize compute_buffer_size(unsigned int scale)
{
    VkDeviceSize side = 2048ull * MAX(scale, 1);
    return MIN(side * side * 8, 800ull << 20);
}

/* The Xbox's RAM imported as a buffer the GPU copies from
 * (VK_EXT_external_memory_host): the whole of it, page aligned as QEMU
 * maps it, in a host-coherent memory type the driver can import it to.
 * Where any of that is not given, no buffer, and every upload goes through
 * the staging ring. */
static void create_vram_buffer(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->vram_buffer = VK_NULL_HANDLE;
    r->vram_memory = VK_NULL_HANDLE;
    if (!r->external_memory_host_extension_enabled ||
        !vkGetMemoryHostPointerPropertiesEXT) {
        return;
    }

    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &host_props,
    };
    vkGetPhysicalDeviceProperties2(r->physical_device, &props2);
    VkDeviceSize align = host_props.minImportedHostPointerAlignment;
    VkDeviceSize size = memory_region_size(d->vram);
    if (!align || ((uintptr_t)d->vram_ptr % align) || (size % align)) {
        return;
    }

    VkMemoryHostPointerPropertiesEXT ptr_props = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
    };
    if (vkGetMemoryHostPointerPropertiesEXT(
            r->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            d->vram_ptr, &ptr_props) != VK_SUCCESS) {
        return;
    }

    VkExternalMemoryBufferCreateInfo external_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
    };
    /* copied from and to, read by the unswizzle shader and written by the
     * swizzle one (surface read-backs) */
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = &external_info,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buffer;
    if (vkCreateBuffer(r->device, &buffer_info, NULL, &buffer) != VK_SUCCESS) {
        return;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(r->device, buffer, &req);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(r->physical_device, &mem_props);
    uint32_t bits = req.memoryTypeBits & ptr_props.memoryTypeBits;
    int type = -1;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount && type < 0; i++) {
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((bits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & want) == want) {
            type = i;
        }
    }
    if (type < 0 || req.size > size) {
        vkDestroyBuffer(r->device, buffer, NULL);
        return;
    }
    VkImportMemoryHostPointerInfoEXT import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        .pHostPointer = d->vram_ptr,
    };
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = size,
        .memoryTypeIndex = type,
    };
    VkDeviceMemory memory;
    if (vkAllocateMemory(r->device, &alloc_info, NULL, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(r->device, buffer, NULL);
        return;
    }
    if (vkBindBufferMemory(r->device, buffer, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(r->device, memory, NULL);
        vkDestroyBuffer(r->device, buffer, NULL);
        return;
    }
    r->vram_buffer = buffer;
    r->vram_memory = memory;
}

static void destroy_vram_buffer(PGRAPHVkState *r)
{
    if (r->vram_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(r->device, r->vram_buffer, NULL);
        vkFreeMemory(r->device, r->vram_memory, NULL);
        r->vram_buffer = VK_NULL_HANDLE;
        r->vram_memory = VK_NULL_HANDLE;
    }
}

/* Query the actual startup buffers, including both ring sets, instead of
 * reserving a guessed host/device margin. No unused mapped host backing. */
static void reserve_buffer_backing(PGRAPHState *pg)
{
    PGRAPHVkState *r=pg->vk_renderer_state;
    VkDeviceSize host=0, device=0;
    const StorageBuffer *buffers[BUFFER_COUNT+8];
    unsigned count=0;
    for (int i=0;i<BUFFER_COUNT;i++) buffers[count++]=&r->storage_buffers[i];
    for (int i=0;i<ARRAY_SIZE(pgraph_vk_staged_buffers);i++) {
        buffers[count++]=&r->storage_buffers[pgraph_vk_staged_buffers[i].staging];
        buffers[count++]=&r->storage_buffers[pgraph_vk_staged_buffers[i].device];
    }
    for (int i=0;i<2;i++) buffers[count++]=&r->vertex_arena[i];
    for (unsigned i=0;i<count;i++) {
        const StorageBuffer *b=buffers[i];
        VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=b->buffer_size,.usage=b->usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        VkBuffer probe;
        VK_CHECK(vkCreateBuffer(r->device,&ci,NULL,&probe));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(r->device,probe,&req);
        vkDestroyBuffer(r->device,probe,NULL);
        VkDeviceSize alignment=MAX(req.alignment,r->device_props.limits.nonCoherentAtomSize);
        VkDeviceSize *bytes=b->alloc_info.host?&host:&device;
        *bytes=ROUND_UP(*bytes,alignment)+ROUND_UP(req.size,alignment);
    }
    VkDeviceSize images=xemu_vk_image_reservation(r->reserved_surface_scale);
    VkResult result=xemu_vk_allocator_reserve(r->allocator,host,device,images);
    if (result!=VK_SUCCESS) {
        fprintf(stderr,"nv2a: Vulkan startup reservation failed: host=%llu MiB, device=%llu MiB, images=%llu MiB (%d)\n",
            (unsigned long long)(host>>20),(unsigned long long)(device>>20),
            (unsigned long long)(images>>20),result);
    }
    VK_CHECK(result);
}

void pgraph_vk_init_buffers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    create_vram_buffer(d);

    // FIXME: Profile buffer sizes

    XemuVkMemoryRequest host_alloc_create_info = {
        .host = true,
    };
    XemuVkMemoryRequest device_alloc_create_info = {
        .host = false,
    };

    r->storage_buffers[BUFFER_STAGING_DST] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = 4096 * 4096 * 4,
    };

    /* The staging ring for uploads (command.c pgraph_vk_staging_alloc),
     * mapped for good */
    r->storage_buffers[BUFFER_STAGING_SRC] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = r->storage_buffers[BUFFER_STAGING_DST].buffer_size,
    };

    /* Fixed upfront capacity; changing resolution does not resize these. */
    r->storage_buffers[BUFFER_COMPUTE_DST] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = compute_buffer_size(r->reserved_surface_scale),
    };

    /* The GPU unswizzle's output ring (unswizzle.c): a level or a surface
     * at a time, 24 MB holds the largest level there is (a 2048 x 2048
     * 32-bit one is 16 MB) with room for the next. Also where a swizzled
     * surface read back is copied linear, for the swizzle shader */
    r->storage_buffers[BUFFER_UNSWIZZLE_OUT] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = 24 * 1024 * 1024,
    };

    r->storage_buffers[BUFFER_COMPUTE_SRC] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size,
    };

    r->storage_buffers[BUFFER_INDEX] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        /* At least one full inline element batch. */
        .buffer_size = MAX(sizeof(pg->inline_elements), 8 * 1024 * 1024),
    };

    r->storage_buffers[BUFFER_INDEX_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_INDEX].buffer_size,
    };

    // FIXME: Don't assume that we can render with host mapped buffer
    r->storage_buffers[BUFFER_VERTEX_RAM] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = memory_region_size(d->vram),
    };

    r->bitmap_size = memory_region_size(d->vram) / TARGET_PAGE_SIZE;
    r->vertex_consumed_bitmap = bitmap_new(r->bitmap_size);
    r->vertex_stale_bitmap = bitmap_new(r->bitmap_size);

    /* Two arenas for copy-on-write snapshots of vertex ranges whose mirror
     * pages unsettled work still reads (draw.c sync_vertex_ram_buffer). */
    for (int i = 0; i < 2; i++) {
        r->vertex_arena[i] = (StorageBuffer){
            .alloc_info = host_alloc_create_info,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .buffer_size = 32 * 1024 * 1024,
        };
    }

    r->storage_buffers[BUFFER_VERTEX_INLINE] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = 16 * 1024 * 1024,
    };

    r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_VERTEX_INLINE].buffer_size,
    };

    r->storage_buffers[BUFFER_UNIFORM] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .buffer_size = 16 * 1024 * 1024,
    };

    r->storage_buffers[BUFFER_UNIFORM_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_UNIFORM].buffer_size,
    };

    /* Immutable palette snapshots share the display descriptor ring's lifetime.
     * Include backing in startup reservation; map once, flush only changed slots. */
    r->storage_buffers[BUFFER_DISPLAY_GAMMA] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .buffer_size = PGRAPH_GAMMA_SLOTS * pgraph_gamma_stride(
            r->device_props.limits.minUniformBufferOffsetAlignment,
            r->device_props.limits.nonCoherentAtomSize),
    };

    reserve_buffer_backing(pg);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        create_buffer(pg, &r->storage_buffers[i]);
    }

    /* Second staging set for the ring rotation, and its device
     * destinations: the next recording writes the other set. */
    for (int i = 0; i < ARRAY_SIZE(pgraph_vk_staged_buffers); i++) {
        const StorageBuffer *staging =
            &r->storage_buffers[pgraph_vk_staged_buffers[i].staging];
        const StorageBuffer *device =
            &r->storage_buffers[pgraph_vk_staged_buffers[i].device];
        r->alt_staging[i] = (StorageBuffer){
            .alloc_info = host_alloc_create_info,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .buffer_size = staging->buffer_size,
        };
        create_buffer(pg, &r->alt_staging[i]);
        VK_CHECK(xemu_vk_map_memory(r->allocator, r->alt_staging[i].allocation,
                              (void **)&r->alt_staging[i].mapped));
        r->alt_device[i] = (StorageBuffer){
            .alloc_info = device->alloc_info,
            .usage = device->usage,
            .buffer_size = device->buffer_size,
        };
        create_buffer(pg, &r->alt_device[i]);
    }

    for (int i = 0; i < 2; i++) {
        create_buffer(pg, &r->vertex_arena[i]);
        r->vertex_arena_dirty_start[i] = UINT64_MAX;
        VkMemoryPropertyFlags props;
        xemu_vk_get_allocation_memory_properties(r->allocator,
                                         r->vertex_arena[i].allocation, &props);
        r->vertex_arena_coherent[i] =
            (props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VK_CHECK(xemu_vk_map_memory(r->allocator, r->vertex_arena[i].allocation,
                              (void **)&r->vertex_arena[i].mapped));
    }

    // FIXME: Add fallback path for device using host mapped memory

    /* the surface downloads' buffer too: mapped once, not at each
     * download (vk/surface.c) */
    VkMemoryPropertyFlags mirror_props;
    xemu_vk_get_allocation_memory_properties(
        r->allocator, r->storage_buffers[BUFFER_VERTEX_RAM].allocation,
        &mirror_props);
    r->vertex_ram_coherent =
        (mirror_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    r->vertex_ram_dirty = (MemorySyncRequirement){ 0 };

    int buffers_to_map[] = { BUFFER_VERTEX_RAM,
                             BUFFER_INDEX_STAGING,
                             BUFFER_VERTEX_INLINE_STAGING,
                             BUFFER_UNIFORM_STAGING,
                             BUFFER_STAGING_SRC,
                             BUFFER_STAGING_DST, BUFFER_DISPLAY_GAMMA };

    for (int i = 0; i < ARRAY_SIZE(buffers_to_map); i++) {
        VK_CHECK(xemu_vk_map_memory(
            r->allocator, r->storage_buffers[buffers_to_map[i]].allocation,
            (void **)&r->storage_buffers[buffers_to_map[i]].mapped));
    }
}

void pgraph_vk_finalize_buffers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        destroy_buffer(pg, &r->storage_buffers[i]);
    }

    for (int i = 0; i < ARRAY_SIZE(pgraph_vk_staged_buffers); i++) {
        destroy_buffer(pg, &r->alt_staging[i]);
        destroy_buffer(pg, &r->alt_device[i]);
    }

    for (int i = 0; i < 2; i++) {
        destroy_buffer(pg, &r->vertex_arena[i]);
    }

    g_free(r->vertex_consumed_bitmap);
    r->vertex_consumed_bitmap = NULL;
    g_free(r->vertex_stale_bitmap);
    r->vertex_stale_bitmap = NULL;

    destroy_vram_buffer(r);
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[index];
    return (ROUND_UP(b->buffer_offset, alignment) + size) <= b->buffer_size;
}

static void recreate_buffer(PGRAPHState *pg, StorageBuffer *b,
                            VkDeviceSize size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    bool mapped = b->mapped != NULL;

    if (mapped) {
        b->mapped = NULL;
    }
    destroy_buffer(pg, b);
    b->buffer_size = size;
    b->buffer_offset = 0;
    create_buffer(pg, b);
    if (mapped) {
        VK_CHECK(xemu_vk_map_memory(r->allocator, b->allocation,
                              (void **)&b->mapped));
    }
}

/* Grow a buffer to hold `needed` bytes, once everything in flight has
 * settled, with its family: a ring, its staging half and both rotation
 * sets, or the two conversion buffers. Upload and readback staging have
 * independent capacities and lifetimes. */
void pgraph_vk_buffer_ensure_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize needed)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (needed <= r->storage_buffers[index].buffer_size) {
        return;
    }

    if (r->in_command_buffer) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    }
    pgraph_vk_wait_for_aux(pg);
    pgraph_vk_settle_all_slots(pg);

    /* A driver may recycle the numeric VkBuffer handle. Invalidate the
     * overlay sets explicitly after draining, before replacing the buffer. */
    if (index == BUFFER_STAGING_SRC) {
        memset(r->display.pvideo.convert.source, 0,
               sizeof(r->display.pvideo.convert.source));
    }

    VkDeviceSize size = MAX(needed, 2 * r->storage_buffers[index].buffer_size);
    size = ROUND_UP(size, 1024 * 1024);

    for (int i = 0; i < ARRAY_SIZE(pgraph_vk_staged_buffers); i++) {
        const StagedBuffer *f = &pgraph_vk_staged_buffers[i];
        if (index == f->staging || index == f->device) {
            recreate_buffer(pg, &r->storage_buffers[f->staging], size);
            recreate_buffer(pg, &r->storage_buffers[f->device], size);
            recreate_buffer(pg, &r->alt_staging[i], size);
            recreate_buffer(pg, &r->alt_device[i], size);
            return;
        }
    }
    if (index == BUFFER_COMPUTE_DST || index == BUFFER_COMPUTE_SRC) {
        recreate_buffer(pg, &r->storage_buffers[BUFFER_COMPUTE_DST], size);
        recreate_buffer(pg, &r->storage_buffers[BUFFER_COMPUTE_SRC], size);
        pgraph_vk_compute_buffers_changed(pg);
        pgraph_vk_unswizzle_packed_buffer_changed(pg);
        return;
    }
    recreate_buffer(pg, &r->storage_buffers[index], size);
    if (index == BUFFER_STAGING_DST) {
        pgraph_vk_compute_buffers_changed(pg);
    }
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDeviceSize total_size = 0;
    for (int i = 0; i < count; i++) {
        total_size += sizes[i];
    }
    assert(pgraph_vk_buffer_has_space_for(pg, index, total_size, alignment));

    StorageBuffer *b = &r->storage_buffers[index];
    VkDeviceSize starting_offset = ROUND_UP(b->buffer_offset, alignment);

    assert(b->mapped);

    for (int i = 0; i < count; i++) {
        b->buffer_offset = ROUND_UP(b->buffer_offset, alignment);
        memcpy(b->mapped + b->buffer_offset, data[i], sizes[i]);
        b->buffer_offset += sizes[i];
    }

    return starting_offset;
}
