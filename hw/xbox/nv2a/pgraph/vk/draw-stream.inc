/* SPDX-License-Identifier: LGPL-2.0-or-later */
/* Shared native streaming; enhancement callbacks compile out of Off. */
static int compare_memory_sync_requirement_by_addr(const void *p1,
                                                   const void *p2)
{
    const MemorySyncRequirement *l = p1, *r = p2;
    if (l->addr < r->addr)
        return -1;
    if (l->addr > r->addr)
        return 1;
    return 0;
}

/* Where a synced range's vertex data is read from this draw: its arena
 * snapshot when cow is set, the mirror otherwise. */
typedef struct VertexCowDispatch {
    bool cow;
    hwaddr lo;
    int slot;
    VkDeviceSize arena_offset;
} VertexCowDispatch;

/* Most adjacent draws revisit a few immutable snapshots. Keep a tiny
 * render-thread-owned index in front of the full containment search. */
static void vertex_cow_remember(PGRAPHVkState *r, unsigned index)
{
    unsigned key = index + 1;
    unsigned n = 0;
    while (n + 1 < ARRAY_SIZE(r->vertex_cow_recent) &&
           r->vertex_cow_recent[n] != key) {
        n++;
    }
    for (; n; n--) {
        r->vertex_cow_recent[n] = r->vertex_cow_recent[n - 1];
    }
    r->vertex_cow_recent[0] = key;
}

static bool vertex_cow_contains(PGRAPHVkState *r,
                                 const VertexCowCacheEntry *e,
                                 hwaddr lo, hwaddr end)
{
    return e->valid && e->addr <= lo && end <= e->addr + e->size &&
           e->epoch == r->vertex_arena_epoch[e->slot];
}

/* A valid snapshot containing [lo, end): containment, not equality, since
 * draws of one buffer use varying element windows, and any subrange of a
 * valid snapshot is exact. */
static VertexCowCacheEntry *vertex_cow_lookup(PGRAPHVkState *r, hwaddr lo,
                                              hwaddr end)
{
    for (unsigned i = 0; i < ARRAY_SIZE(r->vertex_cow_recent); i++) {
        unsigned key = r->vertex_cow_recent[i];
        if (!key) {
            continue;
        }
        VertexCowCacheEntry *e = &r->vertex_cow_cache[key - 1];
        if (vertex_cow_contains(r, e, lo, end)) {
            if (i) {
                vertex_cow_remember(r, key - 1);
            }
            return e;
        }
    }
    for (unsigned c = 0; c < ARRAY_SIZE(r->vertex_cow_cache); c++) {
        VertexCowCacheEntry *e = &r->vertex_cow_cache[c];
        if (vertex_cow_contains(r, e, lo, end)) {
            vertex_cow_remember(r, c);
            return e;
        }
    }
    return NULL;
}

/* Cache the snapshot of [lo, lo + size) at arena offset dst for reuse by
 * later draws of any contained window. */
static void vertex_cow_store(PGRAPHVkState *r, hwaddr lo, VkDeviceSize size,
                             VkDeviceSize dst, unsigned slot)
{
    VertexCowCacheEntry *store = NULL;
    for (int c = 0; c < ARRAY_SIZE(r->vertex_cow_cache); c++) {
        VertexCowCacheEntry *e = &r->vertex_cow_cache[c];
        if (e->valid && e->addr == lo && e->size == size) {
            store = e;
            break;
        }
        if (!store && !e->valid) {
            store = e;
        }
    }
    if (!store) {
        store = &r->vertex_cow_cache[r->vertex_cow_cache_rr++ %
                                     ARRAY_SIZE(r->vertex_cow_cache)];
    }
    *store = (VertexCowCacheEntry){
        .addr = lo,
        .size = size,
        .arena_offset = dst,
        .epoch = r->vertex_arena_epoch[slot],
        .slot = slot,
        .valid = true,
    };
    r->vertex_cow_tail[slot] = store - r->vertex_cow_cache + 1;
    vertex_cow_remember(r, store - r->vertex_cow_cache);
}

/* Only an arena's last allocation can grow without moving bytes.
 * Earlier draws keep their immutable prefix. On non-coherent memory, the
 * tail must start in a separate atom from any existing reader. */
static VertexCowCacheEntry *vertex_cow_extendable(PGRAPHVkState *r,
                                                  hwaddr lo,
                                                  VkDeviceSize size,
                                                  unsigned slot)
{
    unsigned key = r->vertex_cow_tail[slot];
    if (!key || key > ARRAY_SIZE(r->vertex_cow_cache)) {
        return NULL;
    }
    VertexCowCacheEntry *e = &r->vertex_cow_cache[key - 1];
    StorageBuffer *arena = &r->vertex_arena[slot];
    if (!e->valid || e->slot != slot ||
        e->epoch != r->vertex_arena_epoch[slot] || e->addr != lo ||
        size <= e->size || e->arena_offset > arena->buffer_offset ||
        e->size != arena->buffer_offset - e->arena_offset ||
        e->arena_offset > arena->buffer_size ||
        size > arena->buffer_size - e->arena_offset) {
        return NULL;
    }
    if (!r->vertex_arena_coherent[slot] &&
        arena->buffer_offset % r->device_props.limits.nonCoherentAtomSize) {
        return NULL;
    }
    return e;
}

/* A range that unsettled work reads: this draw reads a snapshot of it in an
 * arena (out), or, the arena exhausted, the mirror updated once everything
 * has settled. */
static void vertex_cow_snapshot(PGRAPHState *pg, hwaddr addr,
                                VkDeviceSize size, bool dirty,
                                VertexCowDispatch *out)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    size_t start_bit = addr / TARGET_PAGE_SIZE;
    size_t end_bit = (addr + size) / TARGET_PAGE_SIZE;

    /* Extend the copy downwards so every attribute binding into this range
     * gets a non-negative buffer offset. */
    hwaddr lo = addr;
    for (int a = 0; a < NV2A_VERTEXSHADER_ATTRIBUTES; a++) {
        if (!r->vertex_attr_span[a].size ||
            r->vertex_attribute_to_description_location[a] < 0) {
            continue;
        }
        hwaddr span_page = r->vertex_attr_span[a].addr & TARGET_PAGE_MASK;
        if (span_page >= addr && span_page < addr + size) {
            lo = MIN(lo, r->vertex_attribute_offsets[a]);
        }
    }
    VkDeviceSize copy_size = (addr + size) - lo;

    /* Reuse a snapshot when the range is not dirty: its bytes still equal
     * guest RAM (a newer write invalidates it). */
    VertexCowCacheEntry *hit = dirty ? NULL :
                               vertex_cow_lookup(r, lo, addr + size);
    if (hit) {
        out->cow = true;
        out->lo = hit->addr;
        out->slot = hit->slot;
        out->arena_offset = hit->arena_offset;
        r->vertex_arena_pending = true;
        /* No stale marking: reuse implies !dirty, so the set of
         * mirror-behind pages is unchanged. */
        return;
    }

    if (dirty) {
        /* Snapshots overlapping a guest write hold old content a later
         * contained window would reuse. */
        pgraph_vk_vertex_cow_cache_invalidate(r, lo, copy_size);
    }
    /* Before the arena is picked: a surface download may finish the
     * recording, and under rotation that moves on to the other slot. */
    pgraph_vk_download_surfaces_in_range_if_dirty(pg, lo, copy_size);

    /* Snapshots persist across frames: the arena is append-only, so
     * appending needs no fence wait. Above 3/4 full, recycle only when all
     * submitted readers are already proved complete and this draw holds none. */
    unsigned arena_slot = r->slot & 1;
    StorageBuffer *arena = &r->vertex_arena[arena_slot];
    VertexCowCacheEntry *extend = NULL;
    if (!dirty) {
        extend = vertex_cow_extendable(r, lo, copy_size, arena_slot);
        if (!extend) {
            extend = vertex_cow_extendable(r, lo, copy_size, arena_slot ^ 1);
        }
    }
    if (extend && lo + extend->size > addr &&
        memory_region_test_and_clear_dirty(d->vram, addr,
                                            lo + extend->size - addr,
                                            DIRTY_MEMORY_NV2A)) {
        /* A CPU write during a download/settle must not reuse an older
         * prefix. Only recheck prefix pages that this draw actually reads. */
        dirty = true;
        pgraph_vk_vertex_cow_cache_invalidate(r, lo, copy_size);
        extend = NULL;
    }
    if (extend) {
        arena_slot = extend->slot;
        arena = &r->vertex_arena[arena_slot];
        /* Downloads above may rotate the slot or invalidate the prefix:
         * select it only afterwards. Never overwrite the old allocation. */
        VkDeviceSize tail = copy_size - extend->size;
        nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_1);
        if (!r->vertex_arena_coherent[arena_slot]) {
            r->vertex_arena_dirty_start[arena_slot] =
                MIN(r->vertex_arena_dirty_start[arena_slot], arena->buffer_offset);
        }
        memcpy(arena->mapped + arena->buffer_offset,
               d->vram_ptr + lo + extend->size, tail);
        arena->buffer_offset += tail;
        extend->size = copy_size;
        vertex_cow_remember(r, extend - r->vertex_cow_cache);
        *out = (VertexCowDispatch){
            .cow = true,
            .lo = extend->addr,
            .slot = extend->slot,
            .arena_offset = extend->arena_offset,
        };
        r->vertex_arena_pending = true;
        return;
    }
    /* A new non-coherent snapshot starts in its own flush atom, so making
     * it visible cannot flush an older in-flight reader's atom again. */
    VkDeviceSize alignment = r->vertex_arena_coherent[arena_slot] ? 256 :
        MAX((VkDeviceSize)256, r->device_props.limits.nonCoherentAtomSize);
    VkDeviceSize dst = ROUND_UP(arena->buffer_offset, alignment);
    /* The recording can still read the mirror: retain its consumed marks.
     * Only arena readers are absent here, so no global teardown is needed. */
    if (dst + copy_size > (arena->buffer_size / 4) * 3 &&
        !r->vertex_arena_pending && !r->vertex_arena_used &&
        (int32_t)(r->submit_count - r->settled_submit) <= 0) {
        for (int s = 0; s < 2; s++) {
            r->vertex_arena[s].buffer_offset = 0;
            r->vertex_arena_dirty_start[s] = UINT64_MAX;
            r->vertex_arena_epoch[s]++;
            r->vertex_cow_tail[s] = 0;
        }
        dst = 0;
    }
    if (dst + copy_size > arena->buffer_size) {
        /* Both arenas are append-only and bindings carry their arena index.
         * Use the other unused tail before settling any live readers. */
        unsigned other = arena_slot ^ 1;
        StorageBuffer *spare = &r->vertex_arena[other];
        VkDeviceSize spare_alignment = r->vertex_arena_coherent[other] ? 256 :
            MAX((VkDeviceSize)256, r->device_props.limits.nonCoherentAtomSize);
        VkDeviceSize spare_dst = ROUND_UP(spare->buffer_offset, spare_alignment);
        if (spare_dst <= spare->buffer_size &&
            copy_size <= spare->buffer_size - spare_dst) {
            arena_slot = other;
            arena = spare;
            dst = spare_dst;
        }
    }
    if (dst + copy_size > arena->buffer_size) {
        /* Both arenas exhausted: settle everything (consumed marks clear), then
         * the mirror path is legal again. */
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        pgraph_vk_settle_all_slots(pg);
        pgraph_vk_update_vertex_ram_buffer(pg, addr, d->vram_ptr + addr,
                                           size);
        return;
    }

    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_1);
    if (!r->vertex_arena_coherent[arena_slot]) {
        r->vertex_arena_dirty_start[arena_slot] =
            MIN(r->vertex_arena_dirty_start[arena_slot], dst);
    }
    memcpy(arena->mapped + dst, d->vram_ptr + lo, copy_size);
    arena->buffer_offset = dst + copy_size;
    out->cow = true;
    out->lo = lo;
    out->slot = arena_slot;
    out->arena_offset = dst;
    r->vertex_arena_pending = true;
    /* The mirror falls behind only where the guest wrote: a COW driven by
     * stale alone must not re-mark pages, or the stale set feeds itself. */
    if (dirty) {
        bitmap_set(r->vertex_stale_bitmap, start_bit, end_bit - start_bit);
    }
    vertex_cow_store(r, lo, copy_size, dst, arena_slot);
}

/* Point this draw's attribute bindings at their COW snapshots. */
static void vertex_cow_bind_attributes(PGRAPHVkState *r,
                                       const MemorySyncRequirement *merged,
                                       const VertexCowDispatch *dispatch,
                                       int num_syncs)
{
    for (int a = 0; a < NV2A_VERTEXSHADER_ATTRIBUTES; a++) {
        if (!r->vertex_attr_span[a].size ||
            r->vertex_attribute_to_description_location[a] < 0) {
            continue;
        }
        hwaddr span_page = r->vertex_attr_span[a].addr & TARGET_PAGE_MASK;
        for (int i = 0; i < num_syncs; i++) {
            if (dispatch[i].cow && span_page >= merged[i].addr &&
                span_page < merged[i].addr + merged[i].size) {
                r->vertex_attr_cow[a] = true;
                r->vertex_attr_cow_slot[a] = dispatch[i].slot;
                r->vertex_attr_cow_offset[a] =
                    dispatch[i].arena_offset +
                    (r->vertex_attribute_offsets[a] - dispatch[i].lo);
                break;
            }
        }
    }
}

/* Most draws' interleaved attributes already form one connected span.
 * Coalesce that case while scanning, without sorting or copying records.
 * A gap keeps the general sorted, in-place merge. */
static int merge_vertex_sync_ranges(MemorySyncRequirement *ranges, int count)
{
    assert(count > 0 && count <= NV2A_VERTEXSHADER_ATTRIBUTES);
    hwaddr lo = ranges[0].addr;
    hwaddr hi = lo + ranges[0].size;
    int i;
    for (i = 1; i < count; i++) {
        hwaddr end = ranges[i].addr + ranges[i].size;
        if (ranges[i].addr > hi || end < lo) {
            break;
        }
        lo = MIN(lo, ranges[i].addr);
        hi = MAX(hi, end);
    }
    if (i == count) {
        if (ranges[0].addr != lo || ranges[0].size != hi - lo) {
            ranges[0] = (MemorySyncRequirement){ lo, hi - lo };
        }
        return 1;
    }
    if (count > 1) {
        qsort(ranges, count,
              sizeof(MemorySyncRequirement),
              compare_memory_sync_requirement_by_addr);
    }

    /* Compact into the sorted input: each output is at or behind the input
     * being consumed, so no separate range array is needed. */
    int num_syncs = 1;

    for (int i = 1; i < count; i++) {
        MemorySyncRequirement *p = &ranges[num_syncs - 1];
        MemorySyncRequirement *t = &ranges[i];

        if (t->addr <= (p->addr + p->size)) {
            // Merge with previous
            hwaddr p_end_addr = p->addr + p->size;
            hwaddr t_end_addr = t->addr + t->size;
            hwaddr new_end_addr = MAX(p_end_addr, t_end_addr);
            p->size = new_end_addr - p->addr;
        } else {
            if (num_syncs != i) {
                ranges[num_syncs] = *t;
            }
            num_syncs++;
        }
    }

    return num_syncs;
}

static void sync_vertex_ram_buffer(PGRAPHState *pg)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->num_vertex_ram_buffer_syncs == 0) {
        return;
    }

    // Align sync requirements to page boundaries
    NV2A_VK_DGROUP_BEGIN("Sync vertex RAM buffer");

    for (int i = 0; i < r->num_vertex_ram_buffer_syncs; i++) {
        NV2A_VK_DPRINTF("Need to sync vertex memory @%" HWADDR_PRIx
                        ", %" HWADDR_PRIx " bytes",
                        r->vertex_ram_buffer_syncs[i].addr,
                        r->vertex_ram_buffer_syncs[i].size);

        hwaddr start_addr =
            r->vertex_ram_buffer_syncs[i].addr & TARGET_PAGE_MASK;
        hwaddr end_addr = r->vertex_ram_buffer_syncs[i].addr +
                          r->vertex_ram_buffer_syncs[i].size;
        end_addr = ROUND_UP(end_addr, TARGET_PAGE_SIZE);

        NV2A_VK_DPRINTF("- %d: %08" HWADDR_PRIx " %zd bytes"
                          " -> %08" HWADDR_PRIx " %zd bytes", i,
                        r->vertex_ram_buffer_syncs[i].addr,
                        r->vertex_ram_buffer_syncs[i].size, start_addr,
                        end_addr - start_addr);

        r->vertex_ram_buffer_syncs[i].addr = start_addr;
        r->vertex_ram_buffer_syncs[i].size = end_addr - start_addr;
    }

    MemorySyncRequirement *merged = r->vertex_ram_buffer_syncs;
    int num_syncs = merge_vertex_sync_ranges(
        merged, r->num_vertex_ram_buffer_syncs);

    if (num_syncs < r->num_vertex_ram_buffer_syncs) {
        NV2A_VK_DPRINTF("Reduced to %d sync checks", num_syncs);
    }

    /* A range needing upload (guest-dirty, or mirror stale) goes to the
     * mirror unless unsettled work reads one of its pages; then it is copied
     * into an arena, and only this draw's bindings point at the copy. */
    VertexCowDispatch dispatch[NV2A_VERTEXSHADER_ATTRIBUTES];
    memset(dispatch, 0, num_syncs * sizeof(dispatch[0]));

    for (int i = 0; i < num_syncs; i++) {
        hwaddr addr = merged[i].addr;
        VkDeviceSize size = merged[i].size;
        size_t start_bit = addr / TARGET_PAGE_SIZE;
        size_t end_bit = (addr + size) / TARGET_PAGE_SIZE;

        NV2A_VK_DPRINTF("- %d: %08"HWADDR_PRIx" %zd bytes", i, addr, size);

        bool dirty = memory_region_test_and_clear_dirty(d->vram, addr, size,
                                                        DIRTY_MEMORY_NV2A);
        bool stale = find_next_bit(r->vertex_stale_bitmap, end_bit,
                                   start_bit) < end_bit;
        bool consumed = find_next_bit(r->vertex_consumed_bitmap, end_bit,
                                      start_bit) < end_bit;

        if (dirty || stale) {
            if (!consumed) {
                pgraph_vk_update_vertex_ram_buffer(pg, addr,
                                                   d->vram_ptr + addr, size);
            } else {
                vertex_cow_snapshot(pg, addr, size, dirty, &dispatch[i]);
            }
        }
        bitmap_set(r->vertex_consumed_bitmap, start_bit, end_bit - start_bit);
    }

    vertex_cow_bind_attributes(r, merged, dispatch, num_syncs);

    r->num_vertex_ram_buffer_syncs = 0;

    NV2A_VK_DGROUP_END();
}

#if 0
static void pgraph_vk_debug_attrs(NV2AState *d)
{
    for (int vertex_idx = 0; vertex_idx < pg->draw_arrays_count[i]; vertex_idx++) {
        NV2A_VK_DGROUP_BEGIN("Vertex %d+%d", pg->draw_arrays_start[i], vertex_idx);
        for (int attr_idx = 0; attr_idx < NV2A_VERTEXSHADER_ATTRIBUTES; attr_idx++) {
            VertexAttribute *attr = &pg->vertex_attributes[attr_idx];
            if (attr->count) {
                char *p = (char *)d->vram_ptr + r->attribute_offsets[attr_idx] + (pg->draw_arrays_start[i] + vertex_idx) * attr->stride;
                NV2A_VK_DGROUP_BEGIN("Attribute %d data at %tx", attr_idx, (ptrdiff_t)(p - (char*)d->vram_ptr));
                for (int count_idx = 0; count_idx < attr->count; count_idx++) {
                    switch (attr->format) {
                    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
                        NV2A_VK_DPRINTF("[%d] %f", count_idx, *(float*)p);
                        p += sizeof(float);
                        break;
                    default:
                        assert(0);
                        break;
                    }
                }
                NV2A_VK_DGROUP_END();
            }
        }
        NV2A_VK_DGROUP_END();
    }
}
#endif

static void bind_vertex_buffer(PGRAPHState *pg, uint16_t inline_map,
                               VkDeviceSize offset)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->vertex_arena_pending = false;

    if (r->num_active_vertex_binding_descriptions == 0) {
        return;
    }

    VkBuffer buffers[NV2A_VERTEXSHADER_ATTRIBUTES];
    VkDeviceSize offsets[NV2A_VERTEXSHADER_ATTRIBUTES];

    for (int i = 0; i < r->num_active_vertex_binding_descriptions; i++) {
        int attr_idx = r->vertex_attribute_descriptions[i].location;
        if (inline_map & (1 << attr_idx)) {
            buffers[i] = r->storage_buffers[BUFFER_VERTEX_INLINE].buffer;
            offsets[i] = offset + r->vertex_attribute_offsets[attr_idx];
        } else if (r->vertex_attr_cow[attr_idx]) {
            buffers[i] =
                r->vertex_arena[r->vertex_attr_cow_slot[attr_idx]].buffer;
            offsets[i] = r->vertex_attr_cow_offset[attr_idx];
            r->vertex_arena_used = true;
        } else {
            buffers[i] = r->storage_buffers[BUFFER_VERTEX_RAM].buffer;
            offsets[i] = offset + r->vertex_attribute_offsets[attr_idx];
            /* Self-healing consumed marks: a teardown between sync and
             * bind cleared them; restore before this draw records. */
            if (r->vertex_attr_span[attr_idx].size) {
                size_t sb = (r->vertex_attr_span[attr_idx].addr &
                             TARGET_PAGE_MASK) / TARGET_PAGE_SIZE;
                size_t eb = TARGET_PAGE_ALIGN(
                                r->vertex_attr_span[attr_idx].addr +
                                r->vertex_attr_span[attr_idx].size) /
                            TARGET_PAGE_SIZE;
                bitmap_set(r->vertex_consumed_bitmap, sb, eb - sb);
            }
        }
    }

    vkCmdBindVertexBuffers(r->command_buffer, 0,
                           r->num_active_vertex_binding_descriptions, buffers,
                           offsets);
#if SUN_STREAM
    pgraph_vk_sun_borrow_streams(pg, buffers, offsets, r->num_active_vertex_binding_descriptions);
#endif
}

static void bind_inline_vertex_buffer(PGRAPHState *pg, VkDeviceSize offset)
{
    bind_vertex_buffer(pg, 0xffff, offset);
}

static bool ensure_buffer_space(PGRAPHState *pg, int index, VkDeviceSize size)
{
    if (!pgraph_vk_buffer_has_space_for(pg, index, size, 1)) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        /* The ring is empty now; a request it cannot hold grows it. */
        pgraph_vk_buffer_ensure_capacity(pg, index, size);
        return true;
    }

    return false;
}

static void get_size_and_count_for_format(VkFormat fmt, size_t *size, size_t *count)
{
    static const struct {
        size_t size;
        size_t count;
    } table[] = {
        [VK_FORMAT_R8_UNORM] =              { 1, 1 },
        [VK_FORMAT_R8G8_UNORM] =            { 1, 2 },
        [VK_FORMAT_R8G8B8_UNORM] =          { 1, 3 },
        [VK_FORMAT_R8G8B8A8_UNORM] =        { 1, 4 },
        [VK_FORMAT_R16_SNORM] =             { 2, 1 },
        [VK_FORMAT_R16G16_SNORM] =          { 2, 2 },
        [VK_FORMAT_R16G16B16_SNORM] =       { 2, 3 },
        [VK_FORMAT_R16G16B16A16_SNORM] =    { 2, 4 },
        [VK_FORMAT_R16_SSCALED] =           { 2, 1 },
        [VK_FORMAT_R16G16_SSCALED] =        { 2, 2 },
        [VK_FORMAT_R16G16B16_SSCALED] =     { 2, 3 },
        [VK_FORMAT_R16G16B16A16_SSCALED] =  { 2, 4 },
        [VK_FORMAT_R32_SFLOAT] =            { 4, 1 },
        [VK_FORMAT_R32G32_SFLOAT] =         { 4, 2 },
        [VK_FORMAT_R32G32B32_SFLOAT] =      { 4, 3 },
        [VK_FORMAT_R32G32B32A32_SFLOAT] =   { 4, 4 },
        [VK_FORMAT_R32_SINT] =              { 4, 1 },
    };

    assert(fmt < ARRAY_SIZE(table));
    assert(table[fmt].size);

    *size = table[fmt].size;
    *count = table[fmt].count;
}

typedef struct VertexBufferRemap {
    uint16_t attributes;
    size_t buffer_space_required;
    struct {
        VkDeviceAddress offset;
        VkDeviceSize old_stride;
        /* All reachable vertex formats have at most 16 bytes. Pack the
         * stride and conversion fields into the former 64-bit stride slot. */
        uint8_t new_stride;
        uint8_t read_size;
        uint8_t convert;
    } map[NV2A_VERTEXSHADER_ATTRIBUTES];
} VertexBufferRemap;

static VertexBufferRemap remap_unaligned_attributes(PGRAPHState *pg,
                                                    uint32_t num_vertices)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VertexBufferRemap remap = {0};

    VkDeviceAddress output_offset = 0;

    for (int attr_id = 0; attr_id < NV2A_VERTEXSHADER_ATTRIBUTES; attr_id++) {
        int desc_loc = r->vertex_attribute_to_description_location[attr_id];
        if (desc_loc < 0) {
            continue;
        }

        VkVertexInputBindingDescription *desc =
            &r->vertex_binding_descriptions[desc_loc];
        VkVertexInputAttributeDescription *attr =
            &r->vertex_attribute_descriptions[desc_loc];

        size_t element_size, element_count;
        get_size_and_count_for_format(attr->format, &element_size, &element_count);

        bool offset_valid =
            (r->vertex_attribute_offsets[attr_id] % element_size == 0);
        bool stride_valid = (desc->stride % element_size == 0);

        enum VertexConvert convert = VTX_CONVERT_NONE;
        VkFormat substitute = VK_FORMAT_UNDEFINED;
        if (r->vertex_conversion_required && r->vertex_format_conversion[attr->format]) {
            substitute = pgraph_vk_vertex_substitute_format(attr->format, &convert);
        }
        if (offset_valid && stride_valid && substitute == VK_FORMAT_UNDEFINED) {
            continue;
        }

        size_t read_size = element_size * element_count;
        if (substitute != VK_FORMAT_UNDEFINED) {
            get_size_and_count_for_format(substitute, &element_size, &element_count);
            attr->format = substitute;
        }

        remap.attributes |= 1 << attr_id;
        remap.map[attr_id].offset = ROUND_UP(output_offset, element_size);
        remap.map[attr_id].old_stride = desc->stride;
        remap.map[attr_id].new_stride = element_size * element_count;
        remap.map[attr_id].read_size = read_size;
        remap.map[attr_id].convert = convert;

        // fprintf(stderr,
        //         "attr %02d remapped: "
        //         "%08" HWADDR_PRIx "->%08" HWADDR_PRIx " "
        //         "stride=%d->%zd\n",
        //         attr_id, r->vertex_attribute_offsets[attr_id],
        //         remap.map[attr_id].offset,
        //         remap.map[attr_id].old_stride,
        //         (size_t)remap.map[attr_id].new_stride);

        output_offset =
            remap.map[attr_id].offset +
            (VkDeviceSize)remap.map[attr_id].new_stride * num_vertices;
        desc->stride = remap.map[attr_id].new_stride;
    }

    remap.buffer_space_required = output_offset;

    // reserve space
    if (remap.attributes) {
        StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];
        VkDeviceSize starting_offset = ROUND_UP(buffer->buffer_offset, 16);
        size_t total_space_required =
            (starting_offset - buffer->buffer_offset) + remap.buffer_space_required;
        ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING, total_space_required);
        buffer->buffer_offset = ROUND_UP(buffer->buffer_offset, 16);
    }

    return remap;
}

static void copy_remapped_attributes_to_inline_buffer(PGRAPHState *pg,
                                                      const VertexBufferRemap *remap,
                                                      uint32_t start_vertex,
                                                      uint32_t end_vertex)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];

    if (!remap->attributes) {
        return;
    }

    assert(pgraph_vk_buffer_has_space_for(pg, BUFFER_VERTEX_INLINE_STAGING,
                                          remap->buffer_space_required, 256));

    // FIXME: Caching
    /* [start_vertex, end_vertex), with the original indices preserved:
     * reserve the prefix, but do not read or fill vertices the draw skips. */
    assert(start_vertex <= end_vertex);
    assert(buffer->mapped);
    const uint32_t copy_count = end_vertex - start_vertex;
    /* Do not form a source pointer beyond the last element for an empty
     * range: its trailing stride need not lie within guest RAM. */
    const uint32_t first_vertex = copy_count ? start_vertex : 0;

    /* Dispatch once per attribute, so each small gather copy has a constant
     * width and the compiler can emit unaligned loads/stores directly. */
#define COPY_REMAPPED_WIDTH(n)                                         \
    do {                                                              \
        for (uint32_t vertex_id = 0; vertex_id < copy_count; vertex_id++) { \
            memcpy(out_ptr, in_ptr, (n));                              \
            out_ptr += (n);                                          \
            in_ptr += stride;                                        \
        }                                                             \
    } while (0)

    // Copy vertex data
    for (int attr_id = 0; attr_id < NV2A_VERTEXSHADER_ATTRIBUTES; attr_id++) {
        if (!(remap->attributes & (1 << attr_id))) {
            continue;
        }

        VkDeviceSize attr_buffer_offset =
            buffer->buffer_offset + remap->map[attr_id].offset;

        size_t width = remap->map[attr_id].new_stride;
        size_t stride = remap->map[attr_id].old_stride;
        uint8_t *out_ptr = buffer->mapped + attr_buffer_offset +
                           (size_t)first_vertex * width;
        uint8_t *in_ptr = d->vram_ptr + r->vertex_attribute_offsets[attr_id] +
                          (size_t)first_vertex * stride;

        if (remap->map[attr_id].convert != VTX_CONVERT_NONE) {
            pgraph_vk_convert_vertex_attribute(
                out_ptr, in_ptr, copy_count, stride, width,
                remap->map[attr_id].read_size, remap->map[attr_id].convert);
        } else if (stride == width) {
            /* An unaligned base can require remapping even when the vertex
             * elements are packed. Copy that span once, without gathering. */
            if (copy_count) {
                memcpy(out_ptr, in_ptr, width * copy_count);
            }
        } else {
            switch (width) {
            case 1: COPY_REMAPPED_WIDTH(1); break;
            case 2: COPY_REMAPPED_WIDTH(2); break;
            case 3: COPY_REMAPPED_WIDTH(3); break;
            case 4: COPY_REMAPPED_WIDTH(4); break;
            case 6: COPY_REMAPPED_WIDTH(6); break;
            case 8: COPY_REMAPPED_WIDTH(8); break;
            case 12: COPY_REMAPPED_WIDTH(12); break;
            case 16: COPY_REMAPPED_WIDTH(16); break;
            default:
                COPY_REMAPPED_WIDTH(width);
                break;
            }
        }

        r->vertex_attribute_offsets[attr_id] = attr_buffer_offset;
    }


    buffer->buffer_offset += remap->buffer_space_required;
#undef COPY_REMAPPED_WIDTH
}

/* Only devices requiring conversion enter this path. Retain guest dword
 * padding and the source offsets used to capture provoking-vertex values;
 * emit the new layout directly into the existing mapped upload ring.
 * Keep its plan off the native-format draw path's stack. */
static __attribute__((noinline)) VkDeviceSize upload_converted_inline_array(PGRAPHState *pg,
                                                  VkDeviceSize source_size,
                                                  unsigned source_stride,
                                                  unsigned count)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    struct {
        uint16_t source_offset, destination_offset;
        uint8_t source_width, width, convert, format;
    } plan[NV2A_VERTEXSHADER_ATTRIBUTES];
    unsigned destination_stride = 0;
    bool any_convert = false;
    for (int j = 0; j < r->num_active_vertex_attribute_descriptions; j++) {
        VkVertexInputAttributeDescription *attr = &r->vertex_attribute_descriptions[j];
        enum VertexConvert convert = r->vertex_format_conversion[attr->format];
        VkFormat format = attr->format;
        size_t element, components;
        get_size_and_count_for_format(format, &element, &components);
        plan[j].source_width = element * components;
        if (convert != VTX_CONVERT_NONE) {
            format = pgraph_vk_vertex_substitute_format(format, &convert);
            get_size_and_count_for_format(format, &element, &components);
            any_convert = true;
        }
        assert(r->vertex_attribute_offsets[attr->location] <= UINT16_MAX);
        plan[j].source_offset = r->vertex_attribute_offsets[attr->location];
        plan[j].destination_offset = destination_stride;
        plan[j].width = element * components;
        plan[j].convert = convert;
        assert(format <= UINT8_MAX);
        plan[j].format = format;
        destination_stride += ROUND_UP(plan[j].width, 4);
    }
    VkDeviceSize size = any_convert ? (VkDeviceSize)destination_stride * count : source_size;
    ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING, size);
    if (any_convert) {
        for (int j = 0; j < r->num_active_vertex_attribute_descriptions; j++) {
            VkVertexInputAttributeDescription *attr = &r->vertex_attribute_descriptions[j];
            r->vertex_attribute_offsets[attr->location] = plan[j].destination_offset;
            r->vertex_binding_descriptions[attr->binding].stride = destination_stride;
            attr->format = plan[j].format;
        }
    }
    begin_pre_draw(pg);
    if (!any_convert) {
        void *source = pgraph_inline_array_data(pg);
        return pgraph_vk_update_vertex_inline_buffer(pg, &source, &size, 1);
    }
    StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];
    assert(buffer->mapped && pgraph_vk_buffer_has_space_for(
        pg, BUFFER_VERTEX_INLINE_STAGING, size, 1));
    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_3);
    VkDeviceSize offset = buffer->buffer_offset;
    buffer->buffer_offset += size;
    for (int j = 0; j < r->num_active_vertex_attribute_descriptions; j++) {
        pgraph_vk_convert_vertex_attribute(
            buffer->mapped + offset + plan[j].destination_offset,
            (const uint8_t *)pgraph_inline_array_data(pg) + plan[j].source_offset,
            count, source_stride, destination_stride, plan[j].source_width,
            plan[j].convert);
    }
    return offset;
}

void SUN_FLUSH_DRAW(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!(r->color_binding || r->zeta_binding)) {
        NV2A_VK_DPRINTF("No binding present!!!\n");
        return;
    }

    r->num_vertex_ram_buffer_syncs = 0;

    /* per-vertex cost of the current config (constant for the flush) */
    unsigned long long vcost_milli = 0, vbytes_milli = 0;
    if (vk_cost_probe_active(pg)) {
        vcost_milli = xemu_cost_vert_cost_milli(pg);
        vbytes_milli = xemu_cost_attr_bytes(pg) * 1000ull;
    }

    if (pg->draw_arrays_length) {
        NV2A_VK_DGROUP_BEGIN("Draw Arrays");
        nv2a_profile_inc_counter(NV2A_PROF_DRAW_ARRAYS);

        assert(pg->inline_elements_length == 0);
        assert(pg->inline_buffer_length == 0);
        assert(pg->inline_array_length == 0);

        pgraph_vk_bind_vertex_attributes(d, pg->draw_arrays_min_start,
                                         pg->draw_arrays_max_count - 1, false,
                                         0, pg->draw_arrays_max_count - 1);
        /* DRAW_ARRAYS maintains this bound as batches are appended/connected. */
        uint32_t max_element = pg->draw_arrays_max_count;
        sync_vertex_ram_buffer(pg);
        VertexBufferRemap remap = remap_unaligned_attributes(pg, max_element);

        begin_pre_draw(pg);
        copy_remapped_attributes_to_inline_buffer(
            pg, &remap, pg->draw_arrays_min_start, max_element);
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Draw Arrays");
        begin_draw(pg);
        bind_vertex_buffer(pg, remap.attributes, 0);
        for (int i = 0; i < pg->draw_arrays_length; i++) {
            uint32_t start = pg->draw_arrays_start[i],
                     count = pg->draw_arrays_count[i];
            NV2A_VK_DPRINTF("- [%d] Start:%d Count:%d", i, start, count);
            vkCmdDraw(r->command_buffer, count, 1, start, 0);
            xemu_cost_add_verts(count, vcost_milli, vbytes_milli);
        }
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);

        NV2A_VK_DGROUP_END();
    } else if (pg->inline_elements_length) {
        NV2A_VK_DGROUP_BEGIN("Inline Elements");
        assert(pg->inline_buffer_length == 0);
        assert(pg->inline_array_length == 0);

        nv2a_profile_inc_counter(NV2A_PROF_INLINE_ELEMENTS);

        size_t index_data_size =
            pg->inline_elements_length * sizeof(pg->inline_elements[0]);

        ensure_buffer_space(pg, BUFFER_INDEX_STAGING, index_data_size);

        uint32_t min_element = pg->inline_elements_min;
        uint32_t max_element = pg->inline_elements_max;
        pgraph_vk_bind_vertex_attributes(
            d, min_element, max_element, false, 0,
            pg->inline_elements[pg->inline_elements_length - 1]);
        sync_vertex_ram_buffer(pg);
        VertexBufferRemap remap = remap_unaligned_attributes(pg, max_element + 1);

        begin_pre_draw(pg);
        copy_remapped_attributes_to_inline_buffer(pg, &remap, min_element,
                                                  max_element + 1);
        VkDeviceSize buffer_offset = pgraph_vk_update_index_buffer(
            pg, pg->inline_elements, index_data_size);
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Inline Elements");
        begin_draw(pg);
        bind_vertex_buffer(pg, remap.attributes, 0);
        vkCmdBindIndexBuffer(r->command_buffer,
                             r->storage_buffers[BUFFER_INDEX].buffer,
                             buffer_offset, VK_INDEX_TYPE_UINT32);
#if SUN_STREAM
        pgraph_vk_sun_borrow_index(pg, r->storage_buffers[BUFFER_INDEX].buffer, buffer_offset);
#endif
        vkCmdDrawIndexed(r->command_buffer, pg->inline_elements_length, 1, 0, 0,
                         0);
        xemu_cost_add_verts(pg->inline_elements_length, vcost_milli,
                            vbytes_milli);
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);

        NV2A_VK_DGROUP_END();
    } else if (pg->inline_buffer_length) {
        NV2A_VK_DGROUP_BEGIN("Inline Buffer");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_BUFFERS);
        assert(pg->inline_array_length == 0);

        size_t vertex_data_size = pg->inline_buffer_length * sizeof(float) * 4;
        void *data[NV2A_VERTEXSHADER_ATTRIBUTES];
        size_t sizes[NV2A_VERTEXSHADER_ATTRIBUTES];
        size_t offset = 0;

        pgraph_vk_bind_vertex_attributes_inline(d);
        for (int i = 0; i < r->num_active_vertex_attribute_descriptions; i++) {
            int attr_index = r->vertex_attribute_descriptions[i].location;

            VertexAttribute *attr = &pg->vertex_attributes[attr_index];
            r->vertex_attribute_offsets[attr_index] = offset;

            data[i] = attr->inline_buffer;
            sizes[i] = vertex_data_size;

            attr->inline_buffer_populated = false;
            offset += vertex_data_size;
        }
        ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING, offset);

        begin_pre_draw(pg);
        VkDeviceSize buffer_offset = pgraph_vk_update_vertex_inline_buffer(
            pg, data, sizes, r->num_active_vertex_attribute_descriptions);
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Inline Buffer");
        begin_draw(pg);
        bind_inline_vertex_buffer(pg, buffer_offset);
        vkCmdDraw(r->command_buffer, pg->inline_buffer_length, 1, 0, 0);
        xemu_cost_add_verts(pg->inline_buffer_length, vcost_milli,
                            vbytes_milli);
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);

        NV2A_VK_DGROUP_END();
    } else if (pg->inline_array_length) {
        NV2A_VK_DGROUP_BEGIN("Inline Array");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_ARRAYS);

        VkDeviceSize inline_array_data_size = pg->inline_array_length * 4;

        unsigned int offset = 0;
        for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
            VertexAttribute *attr = &pg->vertex_attributes[i];
            if (attr->count == 0) {
                continue;
            }

            attr->inline_array_offset = offset;
            NV2A_DPRINTF("bind inline attribute %d size=%d, count=%d\n", i,
                         attr->size, attr->count);
            offset += pgraph_inline_attr_size(attr);
        }

        unsigned int vertex_size = offset;
        if (!vertex_size || inline_array_data_size < vertex_size) {
            NV2A_VK_DGROUP_END();
            return;
        }
        unsigned int index_count = pg->inline_array_length * 4 / vertex_size;

        NV2A_DPRINTF("draw inline array %d, %d\n", vertex_size, index_count);
        pgraph_vk_bind_vertex_attributes(d, 0, index_count - 1, true,
                                         vertex_size, index_count - 1);

        VkDeviceSize buffer_offset;
        if (r->vertex_conversion_required) {
            pgraph_materialize_inline_array(pg);
            buffer_offset = upload_converted_inline_array(
                pg, inline_array_data_size, vertex_size, index_count);
        } else {
            ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING,
                                inline_array_data_size);
            begin_pre_draw(pg);
            if (!pgraph_vk_commit_inline_array(pg, inline_array_data_size, &buffer_offset)) {
                void *inline_array_data = pgraph_inline_array_data(pg);
                buffer_offset = pgraph_vk_update_vertex_inline_buffer(
                    pg, &inline_array_data, &inline_array_data_size, 1);
            }
        }
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Inline Array");
        begin_draw(pg);
        bind_inline_vertex_buffer(pg, buffer_offset);
        vkCmdDraw(r->command_buffer, index_count, 1, 0, 0);
        xemu_cost_add_verts(index_count, vcost_milli, vbytes_milli);
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);
        NV2A_VK_DGROUP_END();
    } else {
        NV2A_VK_DPRINTF("EMPTY NV097_SET_BEGIN_END");
        NV2A_UNCONFIRMED("EMPTY NV097_SET_BEGIN_END");
    }
}
