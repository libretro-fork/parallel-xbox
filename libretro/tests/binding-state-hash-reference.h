/* Frozen from f97ffe6; LGPL-2.1-or-later. */
static ShaderBinding *get_shader_binding_for_state(PGRAPHVkState *r,
                                                   const ShaderState *state)
{
    uint64_t hash = fast_hash((void *)state, sizeof(*state));
    LruNode *node = lru_lookup(&r->shader_cache, hash, state);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    NV2A_VK_DPRINTF("shader state hash: %016" PRIx64 " %p", hash, binding);
    return binding;
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
