/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 * Copyright (c) 2026 RÃƒÂ©da ChÃƒÂ©rif-Touil
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

#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "qemu/mstring.h"
#include "renderer.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/pgraph/seed.h"
#include "hw/xbox/nv2a/pgraph/spv/geom.h"
#include "hw/xbox/nv2a/pgraph/glsl/uniform-cache.h"
#include <encodings/xxh3.h>
#include <retro_spsc.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

/* Borrow the immutable vertex/geometry prefix; own only the canonical
 * fragment tail. The split is on an XXH3 stripe boundary. */
enum {
    VK_SHADER_HEAD_BYTES = offsetof(ShaderState, psh) & ~63u,
    VK_SHADER_TAIL_PREFIX = offsetof(ShaderState, psh) - VK_SHADER_HEAD_BYTES,
};
typedef struct ShaderStateTail {
    uint8_t prefix[VK_SHADER_TAIL_PREFIX];
    PshState psh;
} ShaderStateTail;
typedef struct ShaderStateView {
    const ShaderState *head;
    ShaderStateTail tail;
} ShaderStateView;
_Static_assert(offsetof(ShaderStateTail, psh) == VK_SHADER_TAIL_PREFIX,
               "fragment tail must preserve state byte offsets");
_Static_assert(VK_SHADER_HEAD_BYTES + sizeof(ShaderStateTail) == sizeof(ShaderState),
               "fragment tail must cover the complete remaining state");

static inline int vk_pre_state_module_keys(const ShaderState *st,
                                            bool push_constants,
                                            ShaderModuleCacheKey *keys);
static int vk_state_module_keys(const ShaderState *st, bool push_constants,
                                ShaderModuleCacheKey *keys);
static MString *vk_module_glsl(const ShaderModuleCacheKey *key);
static ShaderSource *vk_module_spirv(const ShaderModuleCacheKey *key);
static ShaderSource *vk_spec_take_ready(const ShaderModuleCacheKey *key,
                                         uint64_t h);
static ShaderSource *vk_pre_take_ready(const ShaderModuleCacheKey *key,
                                       uint64_t hash);

const size_t MAX_UNIFORM_ATTR_VALUES_SIZE = NV2A_VERTEXSHADER_ATTRIBUTES * 4 * sizeof(float);

static void create_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    size_t num_sets = ARRAY_SIZE(r->descriptor_sets);

    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 2 * num_sets,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = NV2A_MAX_TEXTURES * num_sets,
        }
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = ARRAY_SIZE(pool_sizes),
        .pPoolSizes = pool_sizes,
        .maxSets = ARRAY_SIZE(r->descriptor_sets),
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &pool_info, NULL,
                                    &r->descriptor_pool));
}

static void destroy_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorPool(r->device, r->descriptor_pool, NULL);
    r->descriptor_pool = VK_NULL_HANDLE;
}

static void create_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayoutBinding bindings[2 + NV2A_MAX_TEXTURES];

    bindings[0] = (VkDescriptorSetLayoutBinding){
        .binding = VSH_UBO_BINDING,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    bindings[1] = (VkDescriptorSetLayoutBinding){
        .binding = PSH_UBO_BINDING,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        bindings[2 + i] = (VkDescriptorSetLayoutBinding){
            .binding = PSH_TEX_BINDING + i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_SIZE(bindings),
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL,
                                         &r->descriptor_set_layout));
}

static void destroy_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorSetLayout(r->device, r->descriptor_set_layout, NULL);
    r->descriptor_set_layout = VK_NULL_HANDLE;
}

static void create_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayout layouts[ARRAY_SIZE(r->descriptor_sets)];
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        layouts[i] = r->descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->descriptor_pool,
        .descriptorSetCount = ARRAY_SIZE(r->descriptor_sets),
        .pSetLayouts = layouts,
    };
    VK_CHECK(
        vkAllocateDescriptorSets(r->device, &alloc_info, r->descriptor_sets));
}

static void destroy_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkFreeDescriptorSets(r->device, r->descriptor_pool,
                         ARRAY_SIZE(r->descriptor_sets), r->descriptor_sets);
    for (int i = 0; i < ARRAY_SIZE(r->descriptor_sets); i++) {
        r->descriptor_sets[i] = VK_NULL_HANDLE;
    }
}

/* Offsets are reusable only before the staging set is reset or rotated.
 * Zero offset is the common reset marker for every buffer-family path. */
static bool uniform_snapshot_valid(PGRAPHVkState *r, ShaderModuleInfo *mod)
{
    VkDeviceSize end = r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset;
    return mod->uniform_staged_valid &&
           mod->uniform_staged_epoch == r->uniform_upload_epoch &&
           mod->uniform_staged_offset <= end &&
           mod->uniforms.total_size <= end - mod->uniform_staged_offset;
}

void pgraph_vk_update_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* Every uniform-staging append goes through this function. A reset,
     * rotation or reallocation leaves offset zero; invalidate old offsets
     * before choosing any snapshot in the newly installed buffer set. */
    unsigned resolve = r->uniforms_changed;
    if (!r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset) {
        r->uniform_upload_epoch++;
        resolve = 3;
    }
    if (!resolve && !r->shader_bindings_changed && !r->texture_bindings_changed &&
        r->descriptor_set_index != r->descriptor_set_base) {
        return;
    }
    ShaderBinding *binding = r->shader_binding;
    ShaderModuleInfo *modules[] = { binding->vsh.module_info,
                                   binding->psh.module_info };
    unsigned uniform_uploads = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(modules); i++) {
        if (!(resolve & (1u << i))) {
            continue; /* selected module and its current offset are unchanged */
        }
        if (uniform_snapshot_valid(r, modules[i])) {
            r->uniform_buffer_offsets[i] = modules[i]->uniform_staged_offset;
        } else {
            uniform_uploads |= 1u << i;
        }
    }
    ShaderUniformLayout *layouts[] = { &binding->vsh.module_info->uniforms,
                                       &binding->psh.module_info->uniforms };
    bool need_uniform_write = uniform_uploads != 0;
    /* All draw pipelines use the same descriptor set layout. A shader
     * switch only needs a new set when its UBO ranges change; the buffer
     * offsets are dynamic draw state. Never modify a recorded set. */
    bool need_descriptor_write = r->texture_bindings_changed ||
        r->descriptor_set_index == r->descriptor_set_base ||
        r->descriptor_uniform_buffer != r->storage_buffers[BUFFER_UNIFORM].buffer ||
        r->descriptor_uniform_sizes[0] != layouts[0]->total_size ||
        r->descriptor_uniform_sizes[1] != layouts[1]->total_size;

    if (!need_uniform_write && !need_descriptor_write) {
        r->uniforms_changed = 0;
        return;
    }

    VkDeviceSize ubo_buffer_total_size = 0;
    /* Each uploaded stage starts at its own alignment boundary. */
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        if (uniform_uploads & (1u << i)) {
            ubo_buffer_total_size = ROUND_UP(
                ubo_buffer_total_size,
                r->device_props.limits.minUniformBufferOffsetAlignment);
            ubo_buffer_total_size += layouts[i]->total_size;
        }
    }
    bool need_ubo_staging_buffer_reset =
        need_uniform_write &&
        !pgraph_vk_buffer_has_space_for(pg, BUFFER_UNIFORM_STAGING,
                                        ubo_buffer_total_size,
                                        r->device_props.limits.minUniformBufferOffsetAlignment);

    /* the slot's half of the sets used up: a synchronous finish, after
     * which the next begin starts the slot's half over */
    bool need_descriptor_write_reset =
        need_descriptor_write && (r->descriptor_set_index >=
         r->descriptor_set_base +
             (int)(ARRAY_SIZE(r->descriptor_sets) / PGRAPH_VK_SLOTS));

    if (need_descriptor_write_reset || need_ubo_staging_buffer_reset) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        r->uniform_upload_epoch++;
        need_uniform_write = true;
        uniform_uploads = 3;
        need_descriptor_write = true;
    }

    VkWriteDescriptorSet descriptor_writes[2 + NV2A_MAX_TEXTURES];

    if (need_uniform_write) {
        for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
            if (!(uniform_uploads & (1u << i))) {
                continue;
            }
            void *data = layouts[i]->allocation;
            VkDeviceSize size = layouts[i]->total_size;
            r->uniform_buffer_offsets[i] = pgraph_vk_append_to_buffer(
                pg, BUFFER_UNIFORM_STAGING, &data, &size, 1,
                r->device_props.limits.minUniformBufferOffsetAlignment);
            modules[i]->uniform_staged_offset = r->uniform_buffer_offsets[i];
            modules[i]->uniform_staged_epoch = r->uniform_upload_epoch;
            modules[i]->uniform_staged_valid = true;
        }
    }
    r->uniforms_changed = 0;

    /* Offsets are draw state, passed at bind time. Uniform-only changes
     * reuse the immutable descriptor set already recorded in this slot. */
    if (!need_descriptor_write) {
        return;
    }
    r->descriptor_uniform_buffer = r->storage_buffers[BUFFER_UNIFORM].buffer;
    for (unsigned i = 0; i < ARRAY_SIZE(layouts); i++) {
        r->descriptor_uniform_sizes[i] = layouts[i]->total_size;
    }
    assert(r->descriptor_set_index < ARRAY_SIZE(r->descriptor_sets));

    VkDescriptorBufferInfo ubo_buffer_infos[2];
    for (int i = 0; i < ARRAY_SIZE(layouts); i++) {
        ubo_buffer_infos[i] = (VkDescriptorBufferInfo){
            .buffer = r->storage_buffers[BUFFER_UNIFORM].buffer,
            .offset = 0,
            .range = layouts[i]->total_size,
        };
        descriptor_writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = r->descriptor_sets[r->descriptor_set_index],
            .dstBinding = i == 0 ? VSH_UBO_BINDING : PSH_UBO_BINDING,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .pBufferInfo = &ubo_buffer_infos[i],
        };
    }

    VkDescriptorImageInfo image_infos[NV2A_MAX_TEXTURES];
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        /* Split samplers: the sampler rides the unit, not the image
         * node (falls back to the node's sampler while unset). */
        VkSampler smp = r->texture_unit_sampler[i] ?
                            r->texture_unit_sampler[i] :
                            r->texture_bindings[i]->sampler;
        image_infos[i] = (VkDescriptorImageInfo){
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = r->texture_bindings[i]->image_view,
            .sampler = smp,
        };
        descriptor_writes[2 + i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = r->descriptor_sets[r->descriptor_set_index],
            .dstBinding = PSH_TEX_BINDING + i,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo = &image_infos[i],
        };
    }

    vkUpdateDescriptorSets(r->device, 6, descriptor_writes, 0, NULL);

    r->descriptor_set_index++;
}

static void update_shader_uniform_locs(ShaderBinding *binding)
{
    for (int i = 0; i < ARRAY_SIZE(binding->vsh.uniform_locs); i++) {
        binding->vsh.uniform_locs[i] = uniform_index(
            &binding->vsh.module_info->uniforms, VshUniformInfo[i].name);
    }

    for (int i = 0; i < ARRAY_SIZE(binding->psh.uniform_locs); i++) {
        binding->psh.uniform_locs[i] = uniform_index(
            &binding->psh.module_info->uniforms, PshUniformInfo[i].name);
    }
}

static ShaderModuleInfo *
get_and_ref_shader_module_for_key(PGRAPHVkState *r,
                                  const ShaderModuleCacheKey *key)
{
    uint64_t hash = fast_hash((void *)key, sizeof(ShaderModuleCacheKey));
    LruNode *node = lru_lookup(&r->shader_module_cache, hash, key);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    pgraph_vk_ref_shader_module(module->module_info);
    return module->module_info;
}

/* Prewarming needs only geometry and vertex keys; no fragment key or
 * fragment-module lookup is constructed on this path. */
void pgraph_vk_ref_pre_state_modules(PGRAPHVkState *r, const ShaderState *st,
                                     ShaderModuleInfo **geom,
                                     ShaderModuleInfo **vsh)
{
    ShaderModuleCacheKey keys[2];
    int n = vk_pre_state_module_keys(st, r->use_push_constants_for_uniform_attrs,
                                     keys);
    int i = 0;
    *geom = NULL;
    if (n == 2) {
        *geom = get_and_ref_shader_module_for_key(r, &keys[i++]);
    }
    *vsh = get_and_ref_shader_module_for_key(r, &keys[i]);
}

/* The modules of a state's pipeline, each referenced once: geometry (NULL
 * when the state needs none), vertex, and fragment when psh is given. */
void pgraph_vk_ref_state_modules(PGRAPHVkState *r, const ShaderState *st,
                                 ShaderModuleInfo **geom,
                                 ShaderModuleInfo **vsh,
                                 ShaderModuleInfo **psh)
{
    ShaderModuleCacheKey keys[3];
    int n = vk_state_module_keys(st, r->use_push_constants_for_uniform_attrs,
                                 keys);
    int i = 0;

    *geom = NULL;
    if (n == 3) {
        *geom = get_and_ref_shader_module_for_key(r, &keys[i++]);
    }
    *vsh = get_and_ref_shader_module_for_key(r, &keys[i++]);
    if (psh) {
        *psh = get_and_ref_shader_module_for_key(r, &keys[i]);
    }
}

static inline QEMU_ALWAYS_INLINE void shader_cache_entry_init_modules(
    Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_cache);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    NV2A_VK_DPRINTF("cache miss");
    nv2a_profile_inc_counter(NV2A_PROF_SHADER_GEN);

    pgraph_vk_ref_state_modules(r, &binding->state, &binding->geom.module_info,
                                &binding->vsh.module_info,
                                &binding->psh.module_info);

    update_shader_uniform_locs(binding);
}

static void shader_cache_entry_init(Lru *lru, LruNode *node, const void *state)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    memcpy(&binding->state, state, sizeof(ShaderState));
    shader_cache_entry_init_modules(lru, node);
}

static void shader_cache_entry_init_view(Lru *lru, LruNode *node, const void *key)
{
    const ShaderStateView *view = key;
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    memcpy(&binding->state, view->head, VK_SHADER_HEAD_BYTES);
    memcpy((uint8_t *)&binding->state + VK_SHADER_HEAD_BYTES,
           &view->tail, sizeof(view->tail));
    shader_cache_entry_init_modules(lru, node);
}

static bool shader_cache_entry_compare_view(Lru *lru, LruNode *node, const void *key)
{
    const ShaderStateView *view = key;
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    return memcmp(&binding->state, view->head, VK_SHADER_HEAD_BYTES) ||
           memcmp((const uint8_t *)&binding->state + VK_SHADER_HEAD_BYTES,
                  &view->tail, sizeof(view->tail));
}

static void shader_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_cache);
    ShaderBinding *snode = container_of(node, ShaderBinding, node);

    ShaderModuleInfo *modules[] = {
        snode->vsh.module_info,
        snode->geom.module_info,
        snode->psh.module_info,
    };
    for (int i = 0; i < ARRAY_SIZE(modules); i++) {
        if (modules[i]) {
            pgraph_vk_unref_shader_module(r, modules[i]);
        }
    }
}

static bool shader_cache_entry_compare(Lru *lru, LruNode *node, const void *key)
{
    ShaderBinding *snode = container_of(node, ShaderBinding, node);
    return memcmp(&snode->state, key, sizeof(ShaderState));
}

static void shader_module_cache_entry_init(Lru *lru, LruNode *node,
                                           const void *key)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    memcpy(&module->key, key, sizeof(ShaderModuleCacheKey));

    /* the worker's SPIR-V when it made this module, else compiled here */
    /* lru_lookup assigns node->hash before invoking this initializer. */
    ShaderSource *spv = vk_spec_take_ready(&module->key, node->hash);
    if (!spv) {
        spv = vk_module_spirv(&module->key);
    }
    module->module_info = pgraph_vk_create_shader_module_from_spirv(r, spv);
    pgraph_vk_ref_shader_module(module->module_info);
}

/* Installed only during the render-thread owner's ready-request adoption.
 * There is no compiler fallback here, and the ordinary cache initializer
 * gains no prewarm branch or lookup. Words transfer directly from their
 * request owner into the native module creation call. */
static void shader_module_cache_entry_init_pre(Lru *lru, LruNode *node,
                                               const void *key)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    memcpy(&module->key, key, sizeof(module->key));
    ShaderSource *spv = vk_pre_take_ready(&module->key, node->hash);
    assert(spv);
    module->module_info = pgraph_vk_create_shader_module_from_spirv(r, spv);
    pgraph_vk_ref_shader_module(module->module_info);
}

static void shader_module_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    pgraph_vk_unref_shader_module(r, module->module_info);
    module->module_info = NULL;
}

static bool shader_module_cache_entry_compare(Lru *lru, LruNode *node,
                                              const void *key)
{
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    return memcmp(&module->key, key, sizeof(ShaderModuleCacheKey));
}

static void shader_cache_init(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* A cache smaller than a game's working set of shader states
     * recompiles forever. */
    const size_t shader_cache_size = 16384;
    lru_init(&r->shader_cache);
    r->shader_cache_entries = g_malloc_n(shader_cache_size, sizeof(ShaderBinding));
    assert(r->shader_cache_entries != NULL);
    for (int i = 0; i < shader_cache_size; i++) {
        lru_add_free(&r->shader_cache, &r->shader_cache_entries[i].node);
    }
    r->shader_cache.init_node = shader_cache_entry_init;
    r->shader_cache.compare_nodes = shader_cache_entry_compare;
    r->shader_cache.post_node_evict = shader_cache_entry_post_evict;

    /* FIXME: Make this configurable */
    const size_t shader_module_cache_size = 50 * 1024;
    lru_init(&r->shader_module_cache);
    r->shader_module_cache_entries =
        g_malloc_n(shader_module_cache_size, sizeof(ShaderModuleCacheEntry));
    assert(r->shader_module_cache_entries != NULL);
    for (int i = 0; i < shader_module_cache_size; i++) {
        lru_add_free(&r->shader_module_cache,
                     &r->shader_module_cache_entries[i].node);
    }

    r->shader_module_cache.init_node = shader_module_cache_entry_init;
    r->shader_module_cache.compare_nodes = shader_module_cache_entry_compare;
    r->shader_module_cache.post_node_evict =
        shader_module_cache_entry_post_evict;
}

static void shader_cache_finalize(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    lru_flush(&r->shader_cache);
    g_free(r->shader_cache_entries);
    r->shader_cache_entries = NULL;

    lru_flush(&r->shader_module_cache);
    g_free(r->shader_module_cache_entries);
    r->shader_module_cache_entries = NULL;
}

static ShaderBinding *get_shader_binding_for_state(PGRAPHVkState *r,
                                                   const ShaderState *state,
                                                   uint64_t hash)
{
    LruNode *node = lru_lookup(&r->shader_cache, hash, state);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    NV2A_VK_DPRINTF("shader state hash: %016" PRIx64 " %p", hash, binding);
    return binding;
}

static ShaderBinding *get_shader_binding_for_view(PGRAPHVkState *r,
                                                 const ShaderStateView *view,
                                                 uint64_t hash)
{
    static const LruKeyOps ops = {
        .init_node = shader_cache_entry_init_view,
        .compare_nodes = shader_cache_entry_compare_view,
    };
    LruNode *node = lru_lookup_with_ops(&r->shader_cache, hash, view, &ops);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    NV2A_VK_DPRINTF("shader state hash: %016" PRIx64 " %p", hash, binding);
    return binding;
}

/* An unseen state draws with its canonical dynamic sibling (same image,
 * state carried in uniforms, as on GL) while a worker compiles its SPIR-V
 * off the draw path and hands the words over, which the next meeting takes.
 * vk_seen holds the states whose modules are known resident (their binding
 * went through get_shader_binding_for_state at least once). Open
 * addressing; a slot lost to collision only means one extra sibling
 * detour. */
#define VK_SEEN_SLOTS 32768
static uint64_t vk_seen[VK_SEEN_SLOTS];

static bool vk_seen_contains(uint64_t h)
{
    unsigned i = h % VK_SEEN_SLOTS;
    for (unsigned p = 0; p < 8; p++, i = (i + 1) % VK_SEEN_SLOTS) {
        if (vk_seen[i] == h) {
            return true;
        }
        if (!vk_seen[i]) {
            return false;
        }
    }
    return false;
}

static void vk_seen_add(uint64_t h)
{
    unsigned i = h % VK_SEEN_SLOTS;
    for (unsigned p = 0; p < 8; p++, i = (i + 1) % VK_SEEN_SLOTS) {
        if (vk_seen[i] == h) {
            return;
        }
        if (!vk_seen[i]) {
            vk_seen[i] = h;
            return;
        }
    }
}

/* The first-meet worker: compiles a state's modules to SPIR-V, off the
 * render thread, and hands the words over. No lock anywhere: states go
 * to the worker through one SPSC ring, the modules come back through
 * another, the worker sleeps on an eventcount, and the track of what is
 * queued and what has landed is the render thread's alone. */
#define VK_SPEC_QUEUED 1      /* its SPIR-V is being compiled */
#define VK_SPEC_ADOPTED 2     /* its SPIR-V landed: its modules can be made */
#define VK_SPEC_PIPE_QUEUED 3 /* its pipeline is being made (draw.c) */
#define VK_SPEC_PIPE_LANDED 4 /* its pipeline is in the cache: draw with it */
static struct { uint64_t hash; int phase; } vk_spec_track[1024];

typedef struct VkSpecDone {
    uint64_t key_hash;
    ShaderModuleCacheKey key;
    ShaderSource *spirv; /* immutable; this record owns one reference */
    uint64_t state_hash; /* set on the record that ends a state */
} VkSpecDone;

#define VK_SPEC_RING 256   /* states queued and not yet drained, at most */
#define VK_SPEC_READY (3 * VK_SPEC_RING) /* modules landed, to be made */
static retro_spsc_t vk_spec_in;   /* ShaderState: render thread -> worker */
static retro_spsc_t vk_spec_out;  /* VkSpecDone: worker -> render thread */
static retro_eventcount_t vk_spec_ec;      /* the worker's sleep */
static retro_eventcount_t vk_spec_idle_ec; /* quiesce waits here */
static retro_atomic_int_t vk_spec_inflight; /* states queued, not handed back */
static retro_atomic_int_t vk_spec_cancel;   /* finalize: skip what is queued */
static unsigned vk_spec_undrained; /* states queued, not yet drained: what
                                      the out ring must have room for (render
                                      thread only) */
static QemuThread vk_spec_thread;
static bool vk_spec_started;
static bool vk_spec_push_constants;

/* Eight immutable key owners, not a second shader payload queue. Only
 * pointers enter the small SPSC ring. QUEUED owns keys through the compiler's
 * last read; READY publishes referenced words to the render-thread owner.
 * ADOPTED retains module references while the pipeline queue is full. */
#define VK_PRE_REQUESTS 8
#define VK_PRE_FREE 0
#define VK_PRE_QUEUED 1
#define VK_PRE_READY 2
#define VK_PRE_ADOPTED 3
#define VK_PRE_CANCELLED 4
typedef struct VkPreRequest {
    ShaderModuleCacheKey keys[2];
    uint64_t hashes[2];
    ShaderSource *words[2];
    ShaderModuleInfo *modules[2];
    VkPrewarmState pipeline;
    unsigned count;
    retro_atomic_int_t phase;
} VkPreRequest;
static VkPreRequest vk_pre_requests[VK_PRE_REQUESTS];
static VkPreRequest *vk_pre_ring[VK_PRE_REQUESTS];
static retro_atomic_int_t vk_pre_write, vk_pre_read;
static unsigned vk_pre_write_local;
static unsigned vk_pre_live; /* render thread only */
static VkPreRequest *vk_pre_ready_lend; /* scoped to synchronous cache adoption */
/* Native PGRAPH owner, render thread only; quiesce clears it before draining
 * results so cache finalization cannot enqueue new device work. */
static PGRAPHState *vk_pre_owner;
static void vk_pre_service(PGRAPHState *pg);

static ShaderSource *vk_pre_take_ready(const ShaderModuleCacheKey *key,
                                       uint64_t hash)
{
    VkPreRequest *request = vk_pre_ready_lend;
    if (request) {
        for (unsigned i = 0; i < request->count; i++) {
            if (request->words[i] && request->hashes[i] == hash &&
                !memcmp(&request->keys[i], key, sizeof(*key))) {
                ShaderSource *words = request->words[i];
                request->words[i] = NULL;
                return words;
            }
        }
    }
    return NULL;
}

/* Modules the worker made, keyed by their cache key's hash: looked up
 * when the module cache makes a module, so that takes the words rather
 * than compiling */
static VkSpecDone vk_spec_ready[VK_SPEC_READY];
static unsigned vk_spec_ready_n;
#define VK_SPEC_READY_HEADROOM 32
/* The window's active prefix is oldest first; its suffix contains free slots.
 * Small headroom amortizes eviction shifts while keeping lookups contiguous. */
static unsigned vk_spec_ready_order_storage[VK_SPEC_READY + VK_SPEC_READY_HEADROOM];
static unsigned *vk_spec_ready_order = vk_spec_ready_order_storage;

static void vk_spec_ready_remove_order(unsigned i, unsigned slot)
{
    if (i == 0 && vk_spec_ready_n > VK_SPEC_READY_HEADROOM) {
        /* Small queues cost less to shift than a window compaction.
         * Keep the active FIFO contiguous without shifting it on adoption.
         * Rotate this free slot behind the existing free suffix. */
        vk_spec_ready_order[VK_SPEC_READY] = slot;
        --vk_spec_ready_n;
        if (++vk_spec_ready_order == vk_spec_ready_order_storage + VK_SPEC_READY_HEADROOM) {
            memmove(vk_spec_ready_order_storage, vk_spec_ready_order,
                    VK_SPEC_READY * sizeof(vk_spec_ready_order[0]));
            vk_spec_ready_order = vk_spec_ready_order_storage;
        }
        return;
    }
    unsigned count = --vk_spec_ready_n - i;
    if (count) {
        memmove(vk_spec_ready_order + i, vk_spec_ready_order + i + 1,
                count * sizeof(vk_spec_ready_order[0]));
    }
    vk_spec_ready_order[vk_spec_ready_n] = slot;
}

static void vk_spec_ready_clear(void)
{
    for (unsigned i = 0; i < vk_spec_ready_n; i++) {
        VkSpecDone *d = &vk_spec_ready[vk_spec_ready_order[i]];
        pgraph_vk_shader_source_unref(d->spirv);
        d->spirv = NULL;
    }
    vk_spec_ready_n = 0;
    if (vk_spec_ready_order != vk_spec_ready_order_storage) {
        memmove(vk_spec_ready_order_storage, vk_spec_ready_order,
                VK_SPEC_READY * sizeof(vk_spec_ready_order[0]));
    }
    vk_spec_ready_order = vk_spec_ready_order_storage;
}

static int vk_spec_find(uint64_t h)
{
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        if (vk_spec_track[i].phase && vk_spec_track[i].hash == h) {
            return i;
        }
    }
    return -1;
}

/* The pre-rasterization prefix shared by prewarm and full module keys. */
static inline int vk_pre_state_module_keys(const ShaderState *st,
                                            bool push_constants,
                                            ShaderModuleCacheKey *keys)
{
    int n = 0;
    bool need_gs = pgraph_glsl_need_geom(&st->geom);
    if (need_gs) {
        memset(&keys[n], 0, sizeof(keys[n]));
        keys[n].kind = VK_SHADER_STAGE_GEOMETRY_BIT;
        keys[n].geom.state = st->geom;
        keys[n].geom.glsl_opts.vulkan = true;
        keys[n].geom.glsl_opts.ui_clamp_stage = pgraph_glsl_ui_clamp_stage(&st->psh);
        n++;
    }
    /* The vertex state is copied next; clear only its surrounding bytes. */
    memset(&keys[n], 0, offsetof(ShaderModuleCacheKey, vsh.state));
    const size_t after_vsh = offsetof(ShaderModuleCacheKey, vsh.state) +
                             sizeof(keys[n].vsh.state);
    memset((char *)&keys[n] + after_vsh, 0, sizeof(keys[n]) - after_vsh);
    keys[n].kind = VK_SHADER_STAGE_VERTEX_BIT;
    keys[n].vsh.state = st->vsh;
    keys[n].vsh.glsl_opts.vulkan = true;
    keys[n].vsh.glsl_opts.prefix_outputs = need_gs;
    keys[n].vsh.glsl_opts.use_push_constants_for_uniform_attrs = push_constants;
    keys[n].vsh.glsl_opts.ubo_binding = VSH_UBO_BINDING;
    n++;
    return n;
}

static int vk_state_module_keys(const ShaderState *st, bool push_constants,
                                ShaderModuleCacheKey *keys)
{
    int n = vk_pre_state_module_keys(st, push_constants, keys);
    /* The fragment state is copied next; retain zeros in the unused union. */
    memset(&keys[n], 0, offsetof(ShaderModuleCacheKey, psh.state));
    const size_t after_psh = offsetof(ShaderModuleCacheKey, psh.state) +
                             sizeof(keys[n].psh.state);
    memset((char *)&keys[n] + after_psh, 0, sizeof(keys[n]) - after_psh);
    keys[n].kind = VK_SHADER_STAGE_FRAGMENT_BIT;
    keys[n].psh.state = st->psh;
    keys[n].psh.glsl_opts.vulkan = true;
    keys[n].psh.glsl_opts.ubo_binding = PSH_UBO_BINDING;
    keys[n].psh.glsl_opts.tex_binding = PSH_TEX_BINDING;
    n++;
    return n;
}

/* The GLSL of a module key */
static MString *vk_module_glsl(const ShaderModuleCacheKey *key)
{
    switch (key->kind) {
    case VK_SHADER_STAGE_VERTEX_BIT:
        return pgraph_glsl_gen_vsh(&key->vsh.state, key->vsh.glsl_opts);
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        return pgraph_glsl_gen_geom(&key->geom.state, key->geom.glsl_opts);
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        return pgraph_glsl_gen_psh(&key->psh.state, key->psh.glsl_opts);
    default:
        assert(!"Invalid shader module kind");
        return NULL;
    }
}

/* Both render-thread misses and worker prewarming use the same emitter.
 * Unsupported stages/states retain the established GLSL compiler. */
static bool vk_module_line_options(const ShaderModuleCacheKey *key,
                                    SpvLineGeomOptions *opts)
{
    if (key->kind != VK_SHADER_STAGE_GEOMETRY_BIT ||
        (!key->geom.glsl_opts.vulkan || key->geom.glsl_opts.ui_clamp_stage)) {
        return false;
    }
    const GeomState *state = &key->geom.state;
    bool line = state->primitive_mode == PRIM_TYPE_LINES ||
                state->primitive_mode == PRIM_TYPE_LINE_LOOP ||
                state->primitive_mode == PRIM_TYPE_LINE_STRIP;
    bool polygon_line = state->primitive_mode == PRIM_TYPE_POLYGON &&
                        state->polygon_front_mode == POLY_MODE_LINE;
    if (!(line || polygon_line) || state->wide_lines ||
        state->polygon_front_mode != state->polygon_back_mode) {
        return false;
    }
    *opts = (SpvLineGeomOptions) {
        .smooth_shading = state->smooth_shading,
        .provoking_vertex = polygon_line || state->first_vertex_is_provoking ? 0 : 1,
        .spirv_1_6 = pgraph_vk_spirv_1_6,
    };
    return true;
}

static bool vk_module_triangle_options(const ShaderModuleCacheKey *key,
                                        SpvTriangleGeomOptions *opts)
{
    if (key->kind != VK_SHADER_STAGE_GEOMETRY_BIT ||
        (!key->geom.glsl_opts.vulkan || key->geom.glsl_opts.ui_clamp_stage)) {
        return false;
    }
    const GeomState *state = &key->geom.state;
    unsigned mode = state->primitive_mode;
    if ((mode != PRIM_TYPE_TRIANGLES && mode != PRIM_TYPE_TRIANGLE_STRIP &&
         mode != PRIM_TYPE_TRIANGLE_FAN && mode != PRIM_TYPE_POLYGON) ||
        state->wide_lines || state->polygon_front_mode != state->polygon_back_mode ||
        (state->polygon_front_mode != POLY_MODE_FILL &&
         state->polygon_front_mode != POLY_MODE_LINE && state->polygon_front_mode != POLY_MODE_POINT) ||
        (mode == PRIM_TYPE_POLYGON && state->polygon_front_mode != POLY_MODE_FILL)) {
        return false;
    }
    unsigned provoking = mode == PRIM_TYPE_POLYGON ? 2 :
                          state->first_vertex_is_provoking ? 0 :
                          mode == PRIM_TYPE_TRIANGLE_FAN ? 1 : 2;
    *opts = (SpvTriangleGeomOptions) {
        .smooth_shading = state->smooth_shading,
        .spirv_1_6 = pgraph_vk_spirv_1_6,
        .z_perspective = state->z_perspective,
        .polygon_mode = state->polygon_front_mode == POLY_MODE_LINE ? SPV_GEOM_LINE :
                        state->polygon_front_mode == POLY_MODE_POINT ? SPV_GEOM_POINT : SPV_GEOM_FILL,
        /* Match get_vertex_order(), including its else case. */
        .rotation_even = state->tri_rot0 == 0 ? 0 : state->tri_rot0 == 1 ? 1 : 2,
        .rotation_odd = state->tri_rot1 == 0 ? 0 : state->tri_rot1 == 1 ? 1 : 2,
        .provoking_even = provoking,
        .provoking_odd = mode == PRIM_TYPE_TRIANGLE_STRIP &&
                         !state->first_vertex_is_provoking ? 1 : provoking,
    };
    return true;
}

static bool vk_module_quad_options(const ShaderModuleCacheKey *key,
                                    SpvQuadGeomOptions *opts)
{
    if (key->kind != VK_SHADER_STAGE_GEOMETRY_BIT ||
        (!key->geom.glsl_opts.vulkan || key->geom.glsl_opts.ui_clamp_stage)) {
        return false;
    }
    const GeomState *state = &key->geom.state;
    if ((state->primitive_mode != PRIM_TYPE_QUADS &&
         state->primitive_mode != PRIM_TYPE_QUAD_STRIP) || state->wide_lines ||
        state->polygon_front_mode != state->polygon_back_mode ||
        (state->polygon_front_mode != POLY_MODE_FILL &&
         state->polygon_front_mode != POLY_MODE_LINE && state->polygon_front_mode != POLY_MODE_POINT)) {
        return false;
    }
    *opts = (SpvQuadGeomOptions) {
        .smooth_shading = state->smooth_shading,
        .spirv_1_6 = pgraph_vk_spirv_1_6,
        .z_perspective = state->z_perspective,
        .polygon_mode = state->polygon_front_mode == POLY_MODE_LINE ? SPV_GEOM_LINE :
                        state->polygon_front_mode == POLY_MODE_POINT ? SPV_GEOM_POINT : SPV_GEOM_FILL,
        .strip = state->primitive_mode == PRIM_TYPE_QUAD_STRIP,
    };
    return true;
}

static ShaderSource *vk_module_spirv(const ShaderModuleCacheKey *key)
{
    SpvLineGeomOptions opts;
    if (vk_module_line_options(key, &opts)) {
        SpvDirectModule module;
        if (!pgraph_spv_line_geom(opts, &module)) {
            abort(); /* selector and emitter disagree */
        }
        return pgraph_vk_shader_source_new(module.words, module.num_words);
    }
    SpvTriangleGeomOptions triangle;
    if (vk_module_triangle_options(key, &triangle)) {
        SpvDirectModule module;
        if (!pgraph_spv_triangle_geom(triangle, &module)) {
            abort();
        }
        return pgraph_vk_shader_source_new(module.words, module.num_words);
    }
    SpvQuadGeomOptions quad;
    if (vk_module_quad_options(key, &quad)) {
        SpvDirectModule module;
        if (!pgraph_spv_quad_geom(quad, &module)) {
            abort();
        }
        return pgraph_vk_shader_source_new(module.words, module.num_words);
    }
    MString *code = vk_module_glsl(key);
    ShaderSource *spirv = pgraph_vk_compile_shader(key->kind,
                                                  mstring_get_str(code));
    mstring_unref(code);
    return spirv;
}

/* Worker-owned memo of immutable module words and layouts, not Vulkan objects.
 * Keys include all generator options; device changes clear it at quiescence.
 * Repeated states share a module without recompiling or copying its words.
 * Direct mapping bounds lookup work; the byte cap bounds retained payload. */
#define VK_SPEC_MODULE_SLOTS 64
#define VK_SPEC_MODULE_BYTES (4u << 20)
static struct {
    uint64_t hash;
    ShaderModuleCacheKey key;
    bool spirv_1_6;
    ShaderSource *spirv;
} vk_spec_modules[VK_SPEC_MODULE_SLOTS];
static size_t vk_spec_module_bytes;
static unsigned vk_spec_module_victim;

static void vk_spec_module_evict(unsigned slot)
{
    if (vk_spec_modules[slot].spirv) {
        vk_spec_module_bytes -= vk_spec_modules[slot].spirv->len;
        pgraph_vk_shader_source_unref(vk_spec_modules[slot].spirv);
        vk_spec_modules[slot].spirv = NULL;
    }
}

/* Called by the worker, or after cancellation and acquire-observing zero
 * in-flight states: the producer alone can queue work, so it stays idle. */
static void vk_spec_modules_clear(void)
{
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_modules); i++) {
        vk_spec_module_evict(i);
    }
    vk_spec_module_victim = 0;
    assert(vk_spec_module_bytes == 0);
}

/* The caller shares this immutable key digest with the completion record. */
static ShaderSource *vk_spec_compile_module(const ShaderModuleCacheKey *key,
                                             uint64_t hash)
{
    unsigned slot = hash % ARRAY_SIZE(vk_spec_modules);
    if (vk_spec_modules[slot].spirv &&
        vk_spec_modules[slot].hash == hash &&
        vk_spec_modules[slot].spirv_1_6 == pgraph_vk_spirv_1_6 &&
        !memcmp(&vk_spec_modules[slot].key, key, sizeof(*key))) {
        return pgraph_vk_shader_source_ref(vk_spec_modules[slot].spirv);
    }
    ShaderSource *spirv = vk_module_spirv(key);
    if (spirv->len > VK_SPEC_MODULE_BYTES) {
        return spirv; /* do not retain unusually large programs */
    }
    vk_spec_module_evict(slot);
    while (vk_spec_module_bytes + spirv->len > VK_SPEC_MODULE_BYTES) {
        vk_spec_module_evict(vk_spec_module_victim);
        vk_spec_module_victim =
            (vk_spec_module_victim + 1) % ARRAY_SIZE(vk_spec_modules);
    }
    vk_spec_modules[slot].hash = hash;
    vk_spec_modules[slot].key = *key;
    vk_spec_modules[slot].spirv_1_6 = pgraph_vk_spirv_1_6;
    vk_spec_modules[slot].spirv = pgraph_vk_shader_source_ref(spirv);
    vk_spec_module_bytes += spirv->len;
    return spirv;
}

/* Obtain each state's immutable module words and hand a reference over.
 * Only the worker accesses the memo while active; compilation touches no
 * Vulkan object; the render thread receives words and layouts through the SPSC. */
static void vk_spec_compile_state(const ShaderState *st)
{
    ShaderModuleCacheKey keys[3];
    int n = vk_state_module_keys(st, vk_spec_push_constants, keys);
    uint64_t state_hash = fast_hash((const uint8_t *)st, sizeof(*st));
    for (int i = 0; i < n; i++) {
        uint64_t key_hash = fast_hash((const uint8_t *)&keys[i], sizeof(keys[i]));
        ShaderSource *spv = vk_spec_compile_module(&keys[i], key_hash);
        VkSpecDone fallback;
        void *record;
        bool reserved = retro_spsc_write_reserve(&vk_spec_out, &record,
                                                sizeof(VkSpecDone));
        if (!reserved) {
            record = &fallback;
        }
        /* The ring is byte-addressed: do not require typed alignment.
         * Preserve padding exactly, then publish only the finished record. */
        char *bytes = record;
        const size_t after_key = offsetof(VkSpecDone, key) + sizeof(keys[i]);
        uint64_t done_hash = i == n - 1 ? state_hash : 0;
        memset(bytes, 0, offsetof(VkSpecDone, key));
        memset(bytes + after_key, 0, sizeof(VkSpecDone) - after_key);
        memcpy(bytes + offsetof(VkSpecDone, key), &keys[i], sizeof(keys[i]));
        memcpy(bytes + offsetof(VkSpecDone, key_hash),
               &key_hash, sizeof(key_hash));
        memcpy(bytes + offsetof(VkSpecDone, spirv), &spv, sizeof(spv));
        memcpy(bytes + offsetof(VkSpecDone, state_hash),
               &done_hash, sizeof(done_hash));
        /* The out ring holds three records for each state the render
         * thread may have queued and not drained (vk_spec_queue bounds
         * that), so it has room; were it ever short, the record is dropped
         * and the state stays drawn as its sibling. */
        if (reserved) {
            retro_spsc_write_end(&vk_spec_out, sizeof(VkSpecDone));
        } else if (retro_spsc_write(&vk_spec_out, &fallback, sizeof(fallback)) !=
                   sizeof(fallback)) {
            pgraph_vk_shader_source_unref(spv);
        }
    }
}

/* The input ring owns the immutable state until read_end. Keep that lend
 * through the compiler's last read instead of copying every complete record
 * to the worker stack. Byte-addressed wrapped/unaligned records retain one
 * bounded snapshot; incomplete records are left for the producer. */
static bool vk_spec_process_input(void)
{
    const void *record;
    size_t contiguous = retro_spsc_read_begin(&vk_spec_in, &record);
    if (contiguous >= sizeof(ShaderState) &&
        (uintptr_t)record % _Alignof(ShaderState) == 0) {
        if (!retro_atomic_load_acquire_int(&vk_spec_cancel)) {
            vk_spec_compile_state(record);
        }
        retro_spsc_read_end(&vk_spec_in, sizeof(ShaderState));
    } else {
        retro_spsc_read_end(&vk_spec_in, 0);
        if (!contiguous ||
            retro_spsc_read_avail(&vk_spec_in) < sizeof(ShaderState)) {
            return false;
        }
        ShaderState state;
        if (retro_spsc_read(&vk_spec_in, &state, sizeof(state)) != sizeof(state)) {
            return false;
        }
        if (!retro_atomic_load_acquire_int(&vk_spec_cancel)) {
            vk_spec_compile_state(&state);
        }
    }
    retro_atomic_fetch_sub_int(&vk_spec_inflight, 1);
    retro_eventcount_notify(&vk_spec_idle_ec);
    return true;
}

static bool vk_pre_process_input(void)
{
    unsigned read = (unsigned)retro_atomic_load_relaxed_int(&vk_pre_read);
    if (read == (unsigned)retro_atomic_load_acquire_int(&vk_pre_write)) {
        return false;
    }
    VkPreRequest *request = vk_pre_ring[read % VK_PRE_REQUESTS];
    if (retro_atomic_load_acquire_int(&vk_spec_cancel)) {
        retro_atomic_store_release_int(&request->phase, VK_PRE_CANCELLED);
    } else {
        for (unsigned i = 0; i < request->count; i++) {
            request->words[i] = vk_spec_compile_module(&request->keys[i],
                                                       request->hashes[i]);
        }
        /* No request read survives publication: the owner may adopt it now. */
        retro_atomic_store_release_int(&request->phase, VK_PRE_READY);
    }
    retro_atomic_store_release_int(&vk_pre_read, (int)(read + 1));
    retro_atomic_fetch_sub_int(&vk_spec_inflight, 1);
    retro_eventcount_notify(&vk_spec_idle_ec);
    return true;
}

static void *vk_spec_worker_fn(void *opaque)
{
    for (;;) {
        bool worked = vk_spec_process_input();
        worked |= vk_pre_process_input();
        if (worked) {
            continue;
        }
        int key = retro_eventcount_prepare_wait(&vk_spec_ec);
        if (retro_spsc_read_avail(&vk_spec_in) ||
            retro_atomic_load_relaxed_int(&vk_pre_read) !=
                retro_atomic_load_acquire_int(&vk_pre_write)) {
            retro_eventcount_cancel_wait(&vk_spec_ec);
            continue;
        }
        retro_eventcount_commit_wait(&vk_spec_ec, key);
    }
    return NULL;
}

static void vk_spec_ready_push(const VkSpecDone *done)
{
    unsigned slot;
    if (vk_spec_ready_n < VK_SPEC_READY) {
        slot = vk_spec_ready_order[vk_spec_ready_n++];
    } else {
        slot = vk_spec_ready_order[0];
        pgraph_vk_shader_source_unref(vk_spec_ready[slot].spirv);
        vk_spec_ready_order[VK_SPEC_READY] = slot;
        if (++vk_spec_ready_order == vk_spec_ready_order_storage + VK_SPEC_READY_HEADROOM) {
            memmove(vk_spec_ready_order_storage, vk_spec_ready_order,
                    VK_SPEC_READY * sizeof(vk_spec_ready_order[0]));
            vk_spec_ready_order = vk_spec_ready_order_storage;
        }
    }
    vk_spec_ready[slot] = *done;
}

/* Lands what the worker handed back: the modules into the ready table,
 * the states' tracks to ADOPTED. Render thread only. */
static void vk_spec_drain(void)
{
    if (!vk_spec_started) {
        return;
    }
    VkSpecDone d;
    while (retro_spsc_read(&vk_spec_out, &d, sizeof(d)) == sizeof(d)) {
        vk_spec_ready_push(&d);
        if (d.state_hash) {
            vk_spec_undrained--;
            int t = vk_spec_find(d.state_hash);
            if (t >= 0) {
                vk_spec_track[t].phase = VK_SPEC_ADOPTED;
            }
        }
    }
    /* Binding already services this completion path on unseen states. Adopt
     * pre-raster words here as well, before a draw could compile them again. */
    if (vk_pre_owner) {
        vk_pre_service(vk_pre_owner);
    }
}

/* Keep ownership/movement register pressure out of unsuccessful scans. */
static ShaderSource * __attribute__((noinline)) vk_spec_ready_take_at(unsigned i)
{
    unsigned slot = vk_spec_ready_order[i];
    VkSpecDone *d = &vk_spec_ready[slot];
    ShaderSource *spv = d->spirv;

    vk_spec_ready_remove_order(i, slot);
    d->spirv = NULL;
    return spv;
}

/* The worker's SPIR-V for a module key, taken out of the ready table,
 * or NULL: the caller then compiles it itself. The digest is reused from
 * the module-cache lookup. */
static ShaderSource *vk_spec_take_ready(const ShaderModuleCacheKey *key,
                                         uint64_t h)
{
    const unsigned *order = vk_spec_ready_order;
    for (unsigned i = 0; i < vk_spec_ready_n; i++) {
        unsigned slot = order[i];
        VkSpecDone *d = &vk_spec_ready[slot];
        if (d->key_hash == h && memcmp(&d->key, key, sizeof(*key)) == 0) {
            /* Keep ownership/movement register pressure out of miss scans. */
            return vk_spec_ready_take_at(i);
        }
    }
    return NULL;
}

/* The worker's track of state h: 0 when untracked. */
static int vk_spec_phase(uint64_t h)
{
    if (!vk_spec_started) {
        return 0;
    }
    vk_spec_drain();
    int t = vk_spec_find(h);
    return t >= 0 ? vk_spec_track[t].phase : 0;
}

/* Frees the track slot of a state whose words or pipeline have landed. */
static void vk_spec_release_adopted(uint64_t h)
{
    int t = vk_spec_find(h);
    if (t >= 0 && (vk_spec_track[t].phase == VK_SPEC_ADOPTED ||
                   vk_spec_track[t].phase == VK_SPEC_PIPE_LANDED)) {
        vk_spec_track[t].phase = 0;
    }
}

/* The pipeline worker's word on a state (draw.c): its whole pipeline is
 * being made, then is in the pipeline cache. */
void pgraph_vk_spec_pipeline_queued(uint64_t state_hash)
{
    int t = vk_spec_find(state_hash);
    if (t >= 0 && vk_spec_track[t].phase == VK_SPEC_ADOPTED) {
        vk_spec_track[t].phase = VK_SPEC_PIPE_QUEUED;
    }
}

void pgraph_vk_spec_pipeline_landed(uint64_t state_hash)
{
    int t = vk_spec_find(state_hash);
    if (t >= 0 && (vk_spec_track[t].phase == VK_SPEC_PIPE_QUEUED ||
                   vk_spec_track[t].phase == VK_SPEC_ADOPTED)) {
        vk_spec_track[t].phase = VK_SPEC_PIPE_LANDED;
    }
}

/* At finalize: the states queued are skipped, the one being compiled is
 * waited for (a fraction of a millisecond), then what landed and every
 * track are dropped. The worker stays, idle, for the next renderer. */
static GArray *vk_seed_pending;
static void vk_pre_clear(PGRAPHVkState *r)
{
    assert(!vk_pre_ready_lend);
    for (unsigned j = 0; j < VK_PRE_REQUESTS; j++) {
        VkPreRequest *request = &vk_pre_requests[j];
        for (unsigned i = 0; i < request->count; i++) {
            if (request->words[i]) {
                pgraph_vk_shader_source_unref(request->words[i]);
                request->words[i] = NULL;
            }
            if (request->modules[i]) {
                pgraph_vk_unref_shader_module(r, request->modules[i]);
                request->modules[i] = NULL;
            }
        }
        request->count = 0;
        retro_atomic_store_relaxed_int(&request->phase, VK_PRE_FREE);
    }
    vk_pre_live = 0;
    vk_pre_owner = NULL;
}

static void vk_pre_service(PGRAPHState *pg)
{
    if (!vk_pre_live) {
        return;
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    for (unsigned j = 0; j < VK_PRE_REQUESTS; j++) {
        VkPreRequest *request = &vk_pre_requests[j];
        int phase = retro_atomic_load_acquire_int(&request->phase);
        if (phase == VK_PRE_READY) {
            assert(!vk_pre_ready_lend);
            void (*saved_init)(Lru *, LruNode *, const void *) =
                r->shader_module_cache.init_node;
            assert(saved_init == shader_module_cache_entry_init);
            r->shader_module_cache.init_node = shader_module_cache_entry_init_pre;
            vk_pre_ready_lend = request;
            for (unsigned i = 0; i < request->count; i++) {
                request->modules[i] = get_and_ref_shader_module_for_key(r,
                                                        &request->keys[i]);
                /* A cache hit needs no words; the request still owns them. */
                if (request->words[i]) {
                    pgraph_vk_shader_source_unref(request->words[i]);
                    request->words[i] = NULL;
                }
            }
            vk_pre_ready_lend = NULL;
            r->shader_module_cache.init_node = saved_init;
            retro_atomic_store_relaxed_int(&request->phase, VK_PRE_ADOPTED);
            phase = VK_PRE_ADOPTED;
        }
        if (phase == VK_PRE_ADOPTED) {
            ShaderModuleInfo *geom = request->count == 2 ? request->modules[0] : NULL;
            ShaderModuleInfo *vsh = request->modules[request->count - 1];
            if (pgraph_vk_pre_library_ready(pg, &request->pipeline, vsh, geom)) {
                /* The pipeline slot consumed or released both references. */
                memset(request->modules, 0, sizeof(request->modules));
                request->count = 0;
                retro_atomic_store_relaxed_int(&request->phase, VK_PRE_FREE);
                vk_pre_live--;
            }
        }
    }
    if (!vk_pre_live) {
        vk_pre_owner = NULL;
    }
}

static void vk_spec_quiesce(PGRAPHVkState *r)
{
    vk_pre_owner = NULL;
    if (!vk_spec_started) {
        return;
    }
    retro_atomic_store_release_int(&vk_spec_cancel, 1);
    for (;;) {
        int key = retro_eventcount_prepare_wait(&vk_spec_idle_ec);
        if (retro_atomic_load_acquire_int(&vk_spec_inflight) == 0) {
            retro_eventcount_cancel_wait(&vk_spec_idle_ec);
            break;
        }
        retro_eventcount_commit_wait(&vk_spec_idle_ec, key);
    }
    retro_atomic_store_release_int(&vk_spec_cancel, 0);
    vk_pre_clear(r);
    vk_spec_drain();
    vk_spec_ready_clear();
    vk_spec_modules_clear();
    vk_spec_undrained = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        vk_spec_track[i].phase = 0;
    }
    if (vk_seed_pending) {
        g_array_free(vk_seed_pending, TRUE);
        vk_seed_pending = NULL;
    }
}

static bool vk_spec_start(void)
{
    if (!vk_spec_started) {
        if (!retro_spsc_init(&vk_spec_in, VK_SPEC_RING * sizeof(ShaderState)) ||
            !retro_spsc_init(&vk_spec_out, 3 * VK_SPEC_RING * sizeof(VkSpecDone)) ||
            !retro_eventcount_init(&vk_spec_ec) ||
            !retro_eventcount_init(&vk_spec_idle_ec)) {
            return false; /* no memory: retain the correct uncovered path */
        }
        for (unsigned i = 0; i < VK_SPEC_READY; i++) {
            vk_spec_ready_order[i] = i;
        }
        vk_spec_started = true;
        qemu_thread_create(&vk_spec_thread, "vk-firstmeet", vk_spec_worker_fn,
                           NULL, QEMU_THREAD_DETACHED);
    }
    return true;
}

/* The render thread exclusively owns the native module LRU. Borrow its
 * entries only during this scan, then pin all exact matches before calling
 * anything that can drain jobs or evict another cache owner. A partial hit
 * acquires nothing; worker preparation remains the miss path. */
static bool vk_pre_ref_resident_modules(PGRAPHVkState *r, VkPreRequest *request)
{
    ShaderModuleInfo *resident[2] = {0};
    for (unsigned i = 0; i < request->count; i++) {
        unsigned bin = lru_hash_to_bin(&r->shader_module_cache, request->hashes[i]);
        LruNode *node;
        QTAILQ_FOREACH(node, &r->shader_module_cache.bins[bin], next_bin) {
            if (node->hash != request->hashes[i]) {
                continue;
            }
            ShaderModuleCacheEntry *entry =
                container_of(node, ShaderModuleCacheEntry, node);
            if (entry->module_info &&
                !memcmp(&entry->key, &request->keys[i], sizeof(entry->key))) {
                resident[i] = entry->module_info;
                break;
            }
        }
        if (!resident[i]) {
            return false;
        }
    }
    for (unsigned i = 0; i < request->count; i++) {
        pgraph_vk_ref_shader_module(resident[i]);
        request->modules[i] = resident[i];
    }
    return true;
}

bool pgraph_vk_queue_pre_modules(PGRAPHState *pg, const ShaderState *state,
                                  const VkPrewarmState *pipeline)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(!vk_pre_owner || vk_pre_owner == pg);
    vk_pre_service(pg);
    if (vk_pre_live == VK_PRE_REQUESTS ||
        vk_pre_write_local - (unsigned)retro_atomic_load_acquire_int(&vk_pre_read) >= VK_PRE_REQUESTS ||
        !vk_spec_start()) {
        return false;
    }
    for (unsigned j = 0; j < VK_PRE_REQUESTS; j++) {
        VkPreRequest *request = &vk_pre_requests[j];
        if (retro_atomic_load_relaxed_int(&request->phase) != VK_PRE_FREE) {
            continue;
        }
        request->count = vk_pre_state_module_keys(state,
                                    r->use_push_constants_for_uniform_attrs, request->keys);
        for (unsigned i = 0; i < request->count; i++) {
            request->hashes[i] = fast_hash((const uint8_t *)&request->keys[i],
                                           sizeof(request->keys[i]));
        }
        /* Other live slots own immutable keys/pipeline metadata until their
         * final adoption. A digest only filters: preserve different shader
         * keys and pipeline states even when their hashes collide. The
         * caller's pipeline is lent only for this synchronous comparison;
         * duplicate requests need no second metadata snapshot or queue job. */
        for (unsigned k = 0; k < VK_PRE_REQUESTS; k++) {
            VkPreRequest *pending = &vk_pre_requests[k];
            int phase = retro_atomic_load_acquire_int(&pending->phase);
            if ((phase == VK_PRE_QUEUED || phase == VK_PRE_READY ||
                 phase == VK_PRE_ADOPTED) && pending->count == request->count &&
                !memcmp(pending->hashes, request->hashes,
                        request->count * sizeof(request->hashes[0])) &&
                !memcmp(pending->keys, request->keys,
                        request->count * sizeof(request->keys[0])) &&
                !memcmp(&pending->pipeline, pipeline, sizeof(*pipeline))) {
                request->count = 0;
                return true;
            }
        }
        if (vk_pre_ref_resident_modules(r, request)) {
            ShaderModuleInfo *geom = request->count == 2 ? request->modules[0] : NULL;
            ShaderModuleInfo *vsh = request->modules[request->count - 1];
            if (pgraph_vk_pre_library_ready(pg, pipeline, vsh, geom)) {
                /* Consumed by the native pipeline slot or released on hit. */
                memset(request->modules, 0, sizeof(request->modules));
                request->count = 0;
                return true;
            }
            /* Pipeline pressure: the final request slot owns both references
             * and this bounded snapshot through its later ADOPTED retry. */
            request->pipeline = *pipeline;
            retro_atomic_store_relaxed_int(&request->phase, VK_PRE_ADOPTED);
            vk_pre_owner = pg;
            vk_pre_live++;
            return true;
        }
        request->pipeline = *pipeline;
        /* Key storage is final before the pointer publication. No key or
         * shader payload clone crosses this queue handoff. */
        retro_atomic_store_relaxed_int(&request->phase, VK_PRE_QUEUED);
        vk_pre_ring[vk_pre_write_local % VK_PRE_REQUESTS] = request;
        vk_pre_owner = pg;
        vk_pre_live++;
        retro_atomic_fetch_add_int(&vk_spec_inflight, 1);
        retro_atomic_store_release_int(&vk_pre_write, (int)++vk_pre_write_local);
        retro_eventcount_notify(&vk_spec_ec);
        return true;
    }
    return false;
}

static inline QEMU_ALWAYS_INLINE void vk_spec_queue_impl(
    uint64_t h, const ShaderState *st, const ShaderStateView *view)
{
    if (!vk_spec_start()) {
        return;
    }
    if (vk_spec_find(h) >= 0) {
        return;
    }
    /* What landed is taken in first; then there is room for the state's
     * modules in the out ring only while fewer than the ring's states are
     * queued and undrained (a cancelled state leaves no record: its slot
     * frees with the tracks at quiesce). */
    vk_spec_drain();
    if (vk_spec_undrained >= VK_SPEC_RING) {
        return; /* full: the next meeting queues it */
    }
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        if (!vk_spec_track[i].phase) {
            size_t written = view ?
                retro_spsc_write_pair(&vk_spec_in, view->head, VK_SHADER_HEAD_BYTES,
                                      &view->tail, sizeof(view->tail)) :
                retro_spsc_write(&vk_spec_in, st, sizeof(*st));
            if (written != sizeof(ShaderState)) {
                return; /* full: the next meeting queues it */
            }
            vk_spec_track[i].hash = h;
            vk_spec_track[i].phase = VK_SPEC_QUEUED;
            vk_spec_undrained++;
            retro_atomic_fetch_add_int(&vk_spec_inflight, 1);
            retro_eventcount_notify(&vk_spec_ec);
            break;
        }
    }
}

static void vk_spec_queue(uint64_t h, const ShaderState *st)
{
    vk_spec_queue_impl(h, st, NULL);
}

static void vk_spec_queue_view(uint64_t h, const ShaderStateView *view)
{
    vk_spec_queue_impl(h, NULL, view);
}

static GArray *vk_pump_states; /* renderer-lifetime covers then seed states */
static bool vk_seed_done;

static void vk_pump_remember(const ShaderState *st)
{
    if (!vk_pump_states) {
        vk_pump_states = g_array_new(FALSE, FALSE, sizeof(ShaderState));
    }
    g_array_append_val(vk_pump_states, *st);
}

const ShaderState *pgraph_vk_seed_pump_states(unsigned *count)
{
    if (!vk_pump_states) {
        *count = 0;
        return NULL;
    }
    *count = vk_pump_states->len;
    return (const ShaderState *)vk_pump_states->data;
}

/* The states the seed names, in the order they are queued: the covers
 * (few dynamic siblings serve many specialised states) first, then the
 * states themselves, so a known state's words are there at its first
 * meeting rather than compiled then. Fed to the worker a ring's worth at a
 * time, per flip (vk_seed_step), as the ring frees. */
static unsigned vk_seed_next;

static void vk_seed_siblings(void)
{
    if (!g_config.perf.shader_seeding) {
        return;
    }
    const char *tag = pgraph_seed_game_tag();
    if (!tag) {
        return;
    }
    unsigned n = 0;
    ShaderState *states = pgraph_seed_dict_read(PGRAPH_SEED_VK_STATES,
                                                sizeof(ShaderState), &n);

    /* Learn like GL: the game's own seed (Vulkan section) is queued with the
     * embedded dictionary, and what the session drew is written back at
     * shutdown (vk_seed_write). */
    unsigned n_local = 0;
    ShaderState *local = pgraph_seed_read(PGRAPH_SEED_VK_STATES,
                                          sizeof(ShaderState), &n_local);
    if (!states && !local) {
        return;
    }
    vk_seed_pending = g_array_new(FALSE, FALSE, sizeof(ShaderState));
    vk_seed_next = 0;
    /* One queue slot per distinct cover. */
    GHashTable *seen = pgraph_seed_hash_set_new();
    for (int src = 0; src < 2; src++) {
        ShaderState *arr = src ? local : states;
        unsigned cnt = src ? n_local : n;
        for (unsigned i = 0; arr && i < cnt; i++) {
            ShaderState fb = arr[i];
            if (!pgraph_glsl_psh_canonicalize_dynamic(&fb.psh)) {
                continue;
            }
            uint64_t h = fast_hash((const uint8_t *)&fb, sizeof(fb));
            if (!pgraph_seed_hash_set_add(seen, h)) {
                continue;
            }
            g_array_append_val(vk_seed_pending, fb);
            vk_pump_remember(&fb);
        }
    }
    for (int src = 0; src < 2; src++) {
        ShaderState *arr = src ? local : states;
        unsigned cnt = src ? n_local : n;
        for (unsigned i = 0; arr && i < cnt; i++) {
            uint64_t h = fast_hash((const uint8_t *)&arr[i], sizeof(arr[i]));
            if (!pgraph_seed_hash_set_add(seen, h)) {
                continue;
            }
            g_array_append_val(vk_seed_pending, arr[i]);
            vk_pump_remember(&arr[i]);
        }
    }
    g_hash_table_destroy(seen);
    g_free(states);
    g_free(local);
}

/* Per flip: the next seed states, as many as the worker's ring takes. */
static void vk_seed_step(void)
{
    if (!vk_seed_pending) {
        return;
    }
    vk_spec_drain();
    while (vk_seed_next < vk_seed_pending->len &&
           vk_spec_undrained < VK_SPEC_RING) {
        const ShaderState *st =
            &g_array_index(vk_seed_pending, ShaderState, vk_seed_next);
        vk_spec_queue(fast_hash((const uint8_t *)st, sizeof(*st)), st);
        vk_seed_next++;
    }
    if (vk_seed_next == vk_seed_pending->len) {
        g_array_free(vk_seed_pending, TRUE);
        vk_seed_pending = NULL;
    }
}

/* True once the worker has landed this state's SPIR-V (or a binding
 * already exists): module creation then takes the words, never compiles. */
bool pgraph_vk_seed_spv_ready_hash(uint64_t h)
{
    if (vk_seen_contains(h)) {
        return true;
    }
    return vk_spec_phase(h) >= VK_SPEC_ADOPTED;
}

struct VkSeedCollect {
    GArray *states;
};

static void vk_seed_collect(Lru *lru, LruNode *node, void *opaque)
{
    struct VkSeedCollect *c = opaque;
    ShaderBinding *b = container_of(node, ShaderBinding, node);
    g_array_append_val(c->states, b->state);
}

/* Union of the seed on disk and what this session drew, capped; a session
 * that met nothing new leaves the file alone (GL rule: a short session
 * must never shrink a seed learned over a long one). */
static void vk_seed_write(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!g_config.perf.shader_seeding) {
        return;
    }
    struct VkSeedCollect c = {
        .states = g_array_new(FALSE, FALSE, sizeof(ShaderState)),
    };
    lru_visit_active(&r->shader_cache, vk_seed_collect, &c);
    unsigned total = 0;
    unsigned added = c.states->len ?
        pgraph_seed_merge(PGRAPH_SEED_VK_STATES, sizeof(ShaderState),
                          c.states->data, c.states->len, 4096, &total) : 0;
    if (added) {
        fprintf(stderr, "nv2a: vk shader seed written (%u states, %u new)\n",
                total, added);
    }
    g_array_free(c.states, TRUE);
}

/* After a vmstate load/flush the source arrays changed behind every cache. */
void pgraph_vk_uniform_sources_invalidate(PGRAPHVkState *r)
{
    r->vsh_uni_src.valid = false;
    r->vsh_uni_src.ff_valid = false;
    r->uni_last_pushed[0] = NULL;
    r->uni_last_pushed[1] = NULL;
    r->uniform_epoch++;
    r->uniform_upload_epoch++;
}

/* Compare directly with this module's packed UBO, not a duplicate value
 * cache. Module lifetime owns the metadata; state loads invalidate it by
 * epoch without walking modules or clearing a large shadow allocation. */
static bool vk_apply_uniform_updates(PGRAPHVkState *r,
                                     ShaderUniformLayout *layout,
                                     const UniformInfo *info, int *locs,
                                     void *values, size_t count,
                                     ShaderModuleInfo *mod,
                                     const int8_t *sections)
{
    bool valid = mod->uniform_valid && mod->uniform_epoch == r->uniform_epoch;
    bool skip[UNISEC__COUNT] = { false };
    if (sections && valid) {
        for (unsigned s = UNISEC_LIVE + 1; s < UNISEC__COUNT; s++) {
            skip[s] = mod->uniform_sec_gen[s] == r->vsh_uni_src.gen[s];
        }
    }
    bool changed = false;
    for (size_t i = 0; i < count; i++) {
        if (locs[i] == -1 || (sections && skip[(unsigned)sections[i]])) {
            continue;
        }
        const void *value = (const char *)values + info[i].val_offs;
        changed |= uniform_update(layout, locs[i], value,
                                   info[i].size * info[i].count, !valid);
    }
    if (changed) {
        mod->uniform_staged_valid = false;
    }
    mod->uniform_valid = true;
    mod->uniform_epoch = r->uniform_epoch;
    if (sections) {
        memcpy(mod->uniform_sec_gen, r->vsh_uni_src.gen,
                sizeof(mod->uniform_sec_gen));
    }
    return changed;
}

static void set_psh_texscale_values(PGRAPHState *pg,
                                    PshUniformValues *psh_values)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int i = 0; i < 4; i++) {
        assert(r->texture_bindings[i] != NULL);
        float scale = r->texture_bindings[i]->key.scale;

        BasicColorFormatInfo f_basic =
            kelvin_color_format_info_map[r->texture_bindings[i]
                                             ->key.state.color_format];
        if (!f_basic.linear) {
            scale = 1.0;
        }

        if (r->texture_bindings[i]->key.state.color_format ==
                NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT &&
            pgraph_is_texture_enabled(pg, i) &&
            !r->texture_bindings[i]->depth_source_copy) {
            scale = -scale;
        }
        psh_values->texScale[i] = scale;
    }
}

/* Uniform sections tracked by their pgraph dirty markers
 * (glsl/uniform-cache.h; on VK only this path reads and clears them): a
 * clean section is not re-read, compared or copied, and no whole-block hash
 * runs per draw. */
static void update_shader_uniforms_tracked(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderBinding *binding = r->shader_binding;
    unsigned changed = 0;

    pgraph_vsh_uniform_source_refresh(pg, &r->vsh_uni_src, &binding->state.vsh,
                                      binding->vsh.uniform_locs);
    if (vk_apply_uniform_updates(
        r, &binding->vsh.module_info->uniforms, VshUniformInfo,
        binding->vsh.uniform_locs, &r->vsh_uni_src.values, VshUniform__COUNT,
        binding->vsh.module_info, pgraph_vsh_uniform_section)) {
        changed |= 1;
    }

    PshUniformValues psh_values;
    pgraph_glsl_set_psh_uniform_values(pg, binding->psh.uniform_locs,
                                       &psh_values);
    set_psh_texscale_values(pg, &psh_values);
    if (vk_apply_uniform_updates(
        r, &binding->psh.module_info->uniforms, PshUniformInfo,
        binding->psh.uniform_locs, &psh_values, PshUniform__COUNT,
        binding->psh.module_info, NULL)) {
        changed |= 2;
    }

    /* Track stage selection for draw-change statistics. Descriptor setup
     * can reuse an immutable snapshot when a selected module is unchanged. */
    if (binding->vsh.module_info != r->uni_last_pushed[0]) {
        changed |= 1;
        r->uni_last_pushed[0] = binding->vsh.module_info;
    }
    if (binding->psh.module_info != r->uni_last_pushed[1]) {
        changed |= 2;
        r->uni_last_pushed[1] = binding->psh.module_info;
    }

    r->uniforms_changed |= changed;
}

static void update_shader_uniforms(PGRAPHState *pg)
{
    NV2A_VK_DGROUP_BEGIN("%s", __func__);

    PGRAPHVkState *r = pg->vk_renderer_state;
    nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND);

    assert(r->shader_binding);
    update_shader_uniforms_tracked(pg);
    nv2a_profile_inc_counter(r->uniforms_changed ?
                                 NV2A_PROF_SHADER_UBO_DIRTY :
                                 NV2A_PROF_SHADER_UBO_NOTDIRTY);

    NV2A_VK_DGROUP_END();
}

/* Readiness is an exact cache query, never a cache initializer. The render
 * thread owns these nodes through the caller's synchronous selection. */
ShaderBinding *pgraph_vk_ready_shader_binding(PGRAPHVkState *r,
                                              const ShaderState *state,
                                              uint64_t hash)
{
    unsigned bin = lru_hash_to_bin(&r->shader_cache, hash);
    LruNode *node;
    QTAILQ_FOREACH(node, &r->shader_cache.bins[bin], next_bin) {
        if (node->hash != hash) {
            continue;
        }
        ShaderBinding *binding = container_of(node, ShaderBinding, node);
        if (!memcmp(&binding->state, state, sizeof(*state)) &&
            binding->vsh.module_info && binding->psh.module_info &&
            (!pgraph_glsl_need_geom(&state->geom) || binding->geom.module_info)) {
            return binding;
        }
    }
    return NULL;
}

/* Called only after an exact ready pipeline and binding have been proven.
 * No shader-cache mutation occurs between probe and this proven-hit touch. */
void pgraph_vk_use_ready_shader_binding(PGRAPHState *pg, ShaderBinding *binding)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderBinding *ready = get_shader_binding_for_state(r, &binding->state,
                                                        binding->node.hash);
    assert(ready == binding);
    r->shader_binding = ready;
    r->shader_bindings_changed = true;
    update_shader_uniforms(pg);
}

static __attribute__((noinline)) ShaderBinding *vk_try_sibling_binding(
    PGRAPHVkState *r, const ShaderState *state, uint64_t h, int phase)
{
    ShaderStateView fb;
    fb.head = state;
    memcpy(&fb.tail.psh, &state->psh, sizeof(fb.tail.psh));
    if (pgraph_glsl_psh_canonicalize_dynamic(&fb.tail.psh)) {
        memcpy(fb.tail.prefix,
               (const uint8_t *)state + VK_SHADER_HEAD_BYTES,
               sizeof(fb.tail.prefix));
        uint64_t fh = xxh3_64_pair(fb.head, VK_SHADER_HEAD_BYTES,
                                   &fb.tail, sizeof(fb.tail), 0);
        int sphase = fh != h ? vk_spec_phase(fh) : 0;
        bool sib_ready =
            fh != h &&
            (vk_seen_contains(fh) ||
             sphase == VK_SPEC_ADOPTED ||
             sphase == VK_SPEC_PIPE_LANDED);
        if (sib_ready) {
            if (sphase == VK_SPEC_ADOPTED ||
                sphase == VK_SPEC_PIPE_LANDED) {
                vk_spec_release_adopted(fh);
            }
            if (!phase) {
                vk_spec_queue(h, state);
            } else if (phase == VK_SPEC_ADOPTED) {
                /* Its words landed: the draw path
                 * hands its pipeline to the worker
                 * under this draw's key. */
                r->ahead_state = *state;
                r->ahead_hash = h;
                r->ahead_valid = true;
            }
            nv2a_profile_inc_counter(
                NV2A_PROF_SHADER_FALLBACK);
            /* Keep the hashed sibling alive through lookup;
             * the cache owns its copy before this scope ends. */
            return get_shader_binding_for_view(r, &fb, fh);
        } else if (fh != h && !sphase) {
            /* The sibling, interpreter code, compiles
             * slower than the state: queue it; the
             * normal path serves until it lands. */
            vk_spec_queue_view(fh, &fb);
        } else if (phase == VK_SPEC_ADOPTED) {
            /* no sibling to draw with: the modules are
             * made from the words, the pipeline here */
            vk_spec_release_adopted(h);
        }
    } else if (phase == VK_SPEC_ADOPTED) {
        vk_spec_release_adopted(h);
    }
    return NULL;
}

void pgraph_vk_bind_shaders(PGRAPHState *pg)
{
    NV2A_VK_DGROUP_BEGIN("%s", __func__);

    PGRAPHVkState *r = pg->vk_renderer_state;

    r->shader_bindings_changed = false;

    if (!r->shader_binding ||
        pgraph_glsl_check_shader_state_dirty(pg, &r->shader_binding->state)) {
        /* Keep hash stripes aligned without constructing a sibling clone. */
        ShaderState new_state QEMU_ALIGNED(64) = pgraph_glsl_get_shader_state(pg);
        if (!r->shader_binding || memcmp(&r->shader_binding->state, &new_state,
                                         sizeof(ShaderState))) {
            uint64_t h =
                fast_hash((const uint8_t *)&new_state, sizeof(new_state));
            uint64_t bound_h = h; /* of the state drawn with, below */
            if (unlikely(!vk_seen_contains(h))) {
                int phase = vk_spec_phase(h);
                if (phase == VK_SPEC_PIPE_LANDED) {
                    /* Its pipeline is in the cache, its modules made
                     * from the worker's words: the normal path below
                     * compiles nothing. */
                    vk_spec_release_adopted(h);
                } else {
                    ShaderBinding *sibling = vk_try_sibling_binding(
                        r, &new_state, h, phase);
                    if (sibling) {
                        r->shader_binding = sibling;
                        bound_h = sibling->node.hash;
                        goto binding_selected;
                    }
                }
            }
            r->shader_binding = get_shader_binding_for_state(r, &new_state, bound_h);
        binding_selected:
            vk_seen_add(bound_h);
            r->shader_bindings_changed = true;
        }
    } else {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND_NOTDIRTY);
    }

    update_shader_uniforms(pg);

    NV2A_VK_DGROUP_END();
}

void pgraph_vk_init_shaders(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    create_descriptor_pool(pg);
    create_descriptor_set_layout(pg);
    create_descriptor_sets(pg);
    shader_cache_init(pg);

    r->use_push_constants_for_uniform_attrs =
        (r->device_props.limits.maxPushConstantsSize >=
         MAX_UNIFORM_ATTR_VALUES_SIZE);
    vk_spec_push_constants = r->use_push_constants_for_uniform_attrs;
    retro_eventcount_init(&r->seed_writeback_complete);
    retro_atomic_store_relaxed_int(&r->seed_writeback_pending, 0);
}

/* Per flip: once the game is named (retail disc name from startup, or
 * Chihiro's detected title), read its seeds and feed the worker a ring's
 * worth per flip. */
void pgraph_vk_seed_service(PGRAPHState *pg)
{
    vk_pre_service(pg);
    if (!vk_seed_done && g_config.perf.shader_seeding && pgraph_seed_game_tag()) {
        vk_seed_done = true;
        vk_seed_siblings();
    }
    if (g_config.perf.shader_seeding) {
        vk_seed_step();
    }
}

/* Shutdown writeback, on the pfifo thread (see pgraph_vk_process_pending):
 * write the learned seed, then release the UI thread waiting in
 * pre_shutdown_wait. Always signals, even when learning is off. */
void pgraph_vk_shader_seed_writeback(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    vk_seed_write(pg);
    pgraph_clear_and_notify(&r->seed_writeback_complete,
                            &r->seed_writeback_pending);
}

void pgraph_vk_finalize_shaders(PGRAPHState *pg)
{
    vk_seed_write(pg);
    retro_eventcount_free(&pg->vk_renderer_state->seed_writeback_complete);
    shader_cache_finalize(pg);
    destroy_descriptor_sets(pg);
    destroy_descriptor_set_layout(pg);
    destroy_descriptor_pool(pg);
    vk_spec_quiesce(pg->vk_renderer_state);
    /* Pipeline finalization already drained jobs, whose keys are owned by
     * their slots/cache entries. No lend into this bank survives a step. */
    if (vk_pump_states) {
        g_array_free(vk_pump_states, TRUE);
        vk_pump_states = NULL;
    }
    vk_seed_done = false;
}
