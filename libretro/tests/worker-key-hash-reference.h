/* Frozen from 790f151; LGPL-2.1-or-later. */
static void vk_spec_compile_state(const ShaderState *st)
{
    ShaderModuleCacheKey keys[3];
    int n = vk_state_module_keys(st, vk_spec_push_constants, keys);
    uint64_t state_hash = fast_hash((const uint8_t *)st, sizeof(*st));
    for (int i = 0; i < n; i++) {
        ShaderSource *spv = vk_spec_compile_module(&keys[i]);
        VkSpecDone d;
        memset(&d, 0, sizeof(d));
        d.key = keys[i];
        d.key_hash = fast_hash((const uint8_t *)&keys[i], sizeof(keys[i]));
        d.spirv = spv;
        d.state_hash = i == n - 1 ? state_hash : 0;
        /* The out ring holds three records for each state the render
         * thread may have queued and not drained (vk_spec_queue bounds
         * that), so it has room; were it ever short, the record is dropped
         * and the state stays drawn as its sibling. */
        if (retro_spsc_write(&vk_spec_out, &d, sizeof(d)) != sizeof(d)) {
            pgraph_vk_shader_source_unref(d.spirv);
        }
    }
}
