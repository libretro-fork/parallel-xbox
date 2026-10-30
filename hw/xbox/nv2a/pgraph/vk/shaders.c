/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
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

#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "qemu/mstring.h"
#include "renderer.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/pgraph/seed.h"
#include "hw/xbox/nv2a/pgraph/glsl/uniform-cache.h"
#include <retro_spsc.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

static int vk_state_module_keys(const ShaderState *st, bool push_constants,
                                ShaderModuleCacheKey *keys);
static MString *vk_module_glsl(const ShaderModuleCacheKey *key);
static GByteArray *vk_spec_take_ready(const ShaderModuleCacheKey *key);

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

void pgraph_vk_update_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    unsigned uniform_uploads = r->uniforms_changed;
    if (!r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset) {
        uniform_uploads = 3;
    }
    bool need_uniform_write = uniform_uploads != 0;
    bool need_descriptor_write = r->shader_bindings_changed ||
        r->texture_bindings_changed ||
        r->descriptor_set_index == r->descriptor_set_base;

    if (!(r->shader_bindings_changed || r->texture_bindings_changed ||
          (r->descriptor_set_index == r->descriptor_set_base) ||
          need_uniform_write)) {
        return; // Nothing changed
    }

    ShaderBinding *binding = r->shader_binding;
    ShaderUniformLayout *layouts[] = { &binding->vsh.module_info->uniforms,
                                       &binding->psh.module_info->uniforms };
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
        }

        r->uniforms_changed = 0;
    }

    /* Offsets are draw state, passed at bind time. Uniform-only changes
     * reuse the immutable descriptor set already recorded in this slot. */
    if (!need_descriptor_write) {
        return;
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

static void shader_cache_entry_init(Lru *lru, LruNode *node, const void *state)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_cache);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    memcpy(&binding->state, state, sizeof(ShaderState));

    NV2A_VK_DPRINTF("cache miss");
    nv2a_profile_inc_counter(NV2A_PROF_SHADER_GEN);

    pgraph_vk_ref_state_modules(r, &binding->state, &binding->geom.module_info,
                                &binding->vsh.module_info,
                                &binding->psh.module_info);

    update_shader_uniform_locs(binding);
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
    GByteArray *spv = vk_spec_take_ready(&module->key);
    if (!spv) {
        MString *code = vk_module_glsl(&module->key);
        spv = pgraph_vk_compile_glsl_to_spv(module->key.kind,
                                            mstring_get_str(code));
        mstring_unref(code);
    }
    module->module_info = pgraph_vk_create_shader_module_from_spirv(r, spv);
    /* A recycled address must not find the previous module's values. */
    uniform_cache_forget(&r->uni_cache, (uintptr_t)module->module_info);
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
                                                   const ShaderState *state)
{
    uint64_t hash = fast_hash((void *)state, sizeof(*state));
    LruNode *node = lru_lookup(&r->shader_cache, hash, state);
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
    uint32_t *words;  /* the module's SPIR-V, malloc'ed by the worker */
    uint32_t nwords;
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

/* Modules the worker made, keyed by their cache key's hash: looked up
 * when the module cache makes a module, so that takes the words rather
 * than compiling */
static VkSpecDone vk_spec_ready[VK_SPEC_READY];
static unsigned vk_spec_ready_n;

static int vk_spec_find(uint64_t h)
{
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        if (vk_spec_track[i].phase && vk_spec_track[i].hash == h) {
            return i;
        }
    }
    return -1;
}

/* The keys of a state's modules, as pgraph_vk_ref_state_modules makes
 * them: geometry when needed, vertex, fragment. */
static int vk_state_module_keys(const ShaderState *st, bool push_constants,
                                ShaderModuleCacheKey *keys)
{
    int n = 0;
    bool need_gs = pgraph_glsl_need_geom(&st->geom);
    if (need_gs) {
        memset(&keys[n], 0, sizeof(keys[n]));
        keys[n].kind = VK_SHADER_STAGE_GEOMETRY_BIT;
        keys[n].geom.state = st->geom;
        keys[n].geom.glsl_opts.vulkan = true;
        n++;
    }
    memset(&keys[n], 0, sizeof(keys[n]));
    keys[n].kind = VK_SHADER_STAGE_VERTEX_BIT;
    keys[n].vsh.state = st->vsh;
    keys[n].vsh.glsl_opts.vulkan = true;
    keys[n].vsh.glsl_opts.prefix_outputs = need_gs;
    keys[n].vsh.glsl_opts.use_push_constants_for_uniform_attrs = push_constants;
    keys[n].vsh.glsl_opts.ubo_binding = VSH_UBO_BINDING;
    n++;
    memset(&keys[n], 0, sizeof(keys[n]));
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

/* Compile every module of one state to SPIR-V and hand the words over.
 * Safe off the render thread: the generators only read the state, the
 * compiler has no shared state, and no Vulkan object is touched. */
static void vk_spec_compile_state(const ShaderState *st)
{
    ShaderModuleCacheKey keys[3];
    int n = vk_state_module_keys(st, vk_spec_push_constants, keys);
    uint64_t state_hash = fast_hash((const uint8_t *)st, sizeof(*st));
    for (int i = 0; i < n; i++) {
        MString *code = vk_module_glsl(&keys[i]);
        GByteArray *spv =
            pgraph_vk_compile_glsl_to_spv(keys[i].kind, mstring_get_str(code));
        mstring_unref(code);
        VkSpecDone d;
        memset(&d, 0, sizeof(d));
        d.key = keys[i];
        d.key_hash = fast_hash((const uint8_t *)&keys[i], sizeof(keys[i]));
        d.nwords = spv->len / sizeof(uint32_t);
        d.words = (uint32_t *)g_byte_array_free(spv, FALSE);
        d.state_hash = i == n - 1 ? state_hash : 0;
        /* The out ring holds three records for each state the render
         * thread may have queued and not drained (vk_spec_queue bounds
         * that), so it has room; were it ever short, the record is dropped
         * and the state stays drawn as its sibling. */
        if (retro_spsc_write(&vk_spec_out, &d, sizeof(d)) != sizeof(d)) {
            free(d.words);
        }
    }
}

static void *vk_spec_worker_fn(void *opaque)
{
    for (;;) {
        ShaderState st;
        if (retro_spsc_read(&vk_spec_in, &st, sizeof(st)) == sizeof(st)) {
            if (!retro_atomic_load_acquire_int(&vk_spec_cancel)) {
                vk_spec_compile_state(&st);
            }
            retro_atomic_fetch_sub_int(&vk_spec_inflight, 1);
            retro_eventcount_notify(&vk_spec_idle_ec);
            continue;
        }
        int key = retro_eventcount_prepare_wait(&vk_spec_ec);
        if (retro_spsc_read_avail(&vk_spec_in)) {
            retro_eventcount_cancel_wait(&vk_spec_ec);
            continue;
        }
        retro_eventcount_commit_wait(&vk_spec_ec, key);
    }
    return NULL;
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
        if (vk_spec_ready_n == VK_SPEC_READY) {
            /* the oldest unused goes: it is compiled again if met */
            free(vk_spec_ready[0].words);
            memmove(&vk_spec_ready[0], &vk_spec_ready[1],
                    (VK_SPEC_READY - 1) * sizeof(vk_spec_ready[0]));
            vk_spec_ready_n--;
        }
        vk_spec_ready[vk_spec_ready_n++] = d;
        if (d.state_hash) {
            vk_spec_undrained--;
            int t = vk_spec_find(d.state_hash);
            if (t >= 0) {
                vk_spec_track[t].phase = VK_SPEC_ADOPTED;
            }
        }
    }
}

/* The worker's SPIR-V for a module key, taken out of the ready table,
 * or NULL: the caller then compiles it itself */
static GByteArray *vk_spec_take_ready(const ShaderModuleCacheKey *key)
{
    uint64_t h = fast_hash((const uint8_t *)key, sizeof(*key));
    for (unsigned i = 0; i < vk_spec_ready_n; i++) {
        VkSpecDone *d = &vk_spec_ready[i];
        if (d->key_hash == h && memcmp(&d->key, key, sizeof(*key)) == 0) {
            GByteArray *spv = g_byte_array_new_take(
                (guint8 *)d->words, d->nwords * sizeof(uint32_t));
            vk_spec_ready_n--;
            memmove(d, d + 1, (vk_spec_ready_n - i) * sizeof(*d));
            return spv;
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
static void vk_spec_quiesce(void)
{
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
    vk_spec_drain();
    for (unsigned i = 0; i < vk_spec_ready_n; i++) {
        free(vk_spec_ready[i].words);
    }
    vk_spec_ready_n = 0;
    vk_spec_undrained = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        vk_spec_track[i].phase = 0;
    }
    if (vk_seed_pending) {
        g_array_free(vk_seed_pending, TRUE);
        vk_seed_pending = NULL;
    }
}

static void vk_spec_queue(uint64_t h, const ShaderState *st)
{
    if (!vk_spec_started) {
        if (!retro_spsc_init(&vk_spec_in, VK_SPEC_RING * sizeof(ShaderState)) ||
            !retro_spsc_init(&vk_spec_out, 3 * VK_SPEC_RING * sizeof(VkSpecDone)) ||
            !retro_eventcount_init(&vk_spec_ec) ||
            !retro_eventcount_init(&vk_spec_idle_ec)) {
            return; /* no memory: every state compiles where it is met */
        }
        vk_spec_started = true;
        qemu_thread_create(&vk_spec_thread, "vk-firstmeet", vk_spec_worker_fn,
                           NULL, QEMU_THREAD_DETACHED);
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
            if (retro_spsc_write(&vk_spec_in, st, sizeof(*st)) != sizeof(*st)) {
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

static GArray *vk_pump_states; /* covers then states, for the library pump */

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
    uniform_cache_invalidate(&r->uni_cache);
}

static void vk_upload_uniform(void *opaque, const UniformInfo *info, int loc,
                              const void *value)
{
    uniform_copy(opaque, loc, (void *)value, 4,
                 info->size * info->count / 4);
}

/* Per-field compare+copy into the module's UBO allocation. Returns
 * whether the allocation content changed (= a re-push is needed). */
static bool vk_apply_uniform_updates(PGRAPHVkState *r,
                                     ShaderUniformLayout *layout,
                                     const UniformInfo *info, int *locs,
                                     void *values, size_t count,
                                     const ShaderModuleInfo *mod,
                                     const int8_t *sections)
{
    int cs = uniform_cache_slot(&r->uni_cache, (uintptr_t)mod);
    return uniform_cache_apply(&r->uni_cache, cs, r->vsh_uni_src.gen, info,
                               locs, values, count, sections,
                               vk_upload_uniform, layout);
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

    /* The UBO offsets hold the last pushed modules' blocks: a module
     * switch re-pushes that stage even with equal fields. */
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

void pgraph_vk_bind_shaders(PGRAPHState *pg)
{
    NV2A_VK_DGROUP_BEGIN("%s", __func__);

    PGRAPHVkState *r = pg->vk_renderer_state;

    r->shader_bindings_changed = false;

    if (!r->shader_binding ||
        pgraph_glsl_check_shader_state_dirty(pg, &r->shader_binding->state)) {
        ShaderState new_state = pgraph_glsl_get_shader_state(pg);
        if (!r->shader_binding || memcmp(&r->shader_binding->state, &new_state,
                                         sizeof(ShaderState))) {
            uint64_t h =
                fast_hash((const uint8_t *)&new_state, sizeof(new_state));
            uint64_t bound_h = h; /* of the state drawn with, below */
            if (!vk_seen_contains(h)) {
                int phase = vk_spec_phase(h);
                if (phase == VK_SPEC_PIPE_LANDED) {
                    /* Its pipeline is in the cache, its modules made
                     * from the worker's words: the normal path below
                     * compiles nothing. */
                    vk_spec_release_adopted(h);
                } else {
                    ShaderState fb = new_state;
                    if (pgraph_glsl_psh_canonicalize_dynamic(&fb.psh)) {
                        uint64_t fh = fast_hash((const uint8_t *)&fb,
                                                sizeof(fb));
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
                                vk_spec_queue(h, &new_state);
                            } else if (phase == VK_SPEC_ADOPTED) {
                                /* Its words landed: the draw path
                                 * hands its pipeline to the worker
                                 * under this draw's key. */
                                r->ahead_state = new_state;
                                r->ahead_hash = h;
                                r->ahead_valid = true;
                            }
                            nv2a_profile_inc_counter(
                                NV2A_PROF_SHADER_FALLBACK);
                            new_state = fb;
                            bound_h = fh;
                        } else if (fh != h && !sphase) {
                            /* The sibling, interpreter code, compiles
                             * slower than the state: queue it; the
                             * normal path serves until it lands. */
                            vk_spec_queue(fh, &fb);
                        } else if (phase == VK_SPEC_ADOPTED) {
                            /* no sibling to draw with: the modules are
                             * made from the words, the pipeline here */
                            vk_spec_release_adopted(h);
                        }
                    } else if (phase == VK_SPEC_ADOPTED) {
                        vk_spec_release_adopted(h);
                    }
                }
            }
            r->shader_binding = get_shader_binding_for_state(r, &new_state);
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

/* Per flip: once the boot has named the game (the seeds are keyed by the
 * game's name, known at the latest a few seconds into SEGABOOT) the seed
 * is read, then fed to the worker a ring's worth per flip. */
void pgraph_vk_seed_service(void)
{
    static bool done;
    if (!done && pgraph_seed_game_tag()) {
        done = true;
        vk_seed_siblings();
    }
    vk_seed_step();
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
    vk_spec_quiesce();
}
