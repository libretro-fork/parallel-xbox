/* Frozen from 8efd128; LGPL-2.1-or-later. */
static ShaderSource *vk_spec_take_ready(const ShaderModuleCacheKey *key)
{
    uint64_t h = xxh3_64((const uint8_t *)key, sizeof(*key), 0);
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
static void shader_module_cache_entry_init(Lru *lru, LruNode *node,
                                           const void *key)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    memcpy(&module->key, key, sizeof(ShaderModuleCacheKey));

    /* the worker's SPIR-V when it made this module, else compiled here */
    ShaderSource *spv = vk_spec_take_ready(&module->key);
    if (!spv) {
        spv = vk_module_spirv(&module->key);
    }
    module->module_info = pgraph_vk_create_shader_module_from_spirv(r, spv);
    pgraph_vk_ref_shader_module(module->module_info);
}
