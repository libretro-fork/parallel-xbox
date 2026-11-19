/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Finite GS owners use existing module-pool positions. Packed controls are
 * metadata in the existing final UBO upload owner, never a geometry snapshot. */
static ShaderModuleInfo *vk_ready_geometry_find(PGRAPHVkState *r,
                                                const ShaderModuleCacheKey *key)
{
    if (!r->ready_geometry.ready || !r->shader_objects.enabled ||
        key->native_kind != 1 || key->kind != VK_SHADER_STAGE_GEOMETRY_BIT ||
        !key->geom.glsl_opts.vulkan) return NULL;
    int variant = pgraph_glsl_geom_dynamic_variant(&key->geom.state,
                                                   key->geom.glsl_opts);
    return variant < 0 ? NULL : r->ready_geometry.stages[variant].module_info;
}

static void vk_ready_geometry_finalize(PGRAPHVkState *r)
{
    r->ready_geometry.ready = false;
    if (!r->ready_geometry.stages) return;
    for (unsigned i = 0; i < VK_READY_GEOMETRY_STAGES; i++) {
        ShaderModuleCacheEntry *entry = &r->ready_geometry.stages[i];
        if (entry->module_info) pgraph_vk_unref_shader_module(r, entry->module_info);
        memset(entry, 0, sizeof(*entry));
    }
    r->ready_geometry.stages = NULL;
}

/* Startup failure returns unused reservations; no recorded GPU consumer yet. */
static void vk_ready_geometry_decline(PGRAPHVkState *r)
{
    ShaderModuleCacheEntry *reserved = r->ready_geometry.stages;
    if (!reserved) return;
    vk_ready_geometry_finalize(r);
    for (unsigned i = 0; i < VK_READY_GEOMETRY_STAGES; i++)
        lru_add_free(&r->shader_module_cache, &reserved[i].node);
}

static bool vk_ready_geometry_init(PGRAPHVkState *r)
{
    if (!r->ready_geometry.stages) return !r->has_gs_ubo_binding;
    static const char *const names[] = {
        "gsControl", "gsSurfaceSize", "gsLineWidth", "gsKeepWinding"
    };
    static const unsigned offsets[] = {0, 16, 24, 28};
    static const unsigned widths[] = {4, 2, 1, 1};
    for (unsigned i = 0; i < VK_READY_GEOMETRY_STAGES; i++) {
        ShaderModuleCacheEntry *entry = &r->ready_geometry.stages[i];
        memset(entry, 0, sizeof(*entry));
        entry->key.kind = VK_SHADER_STAGE_GEOMETRY_BIT;
        entry->key.native_kind = 1;
        entry->key.geom.ready_kind = 1;
        entry->key.geom.ready_version = VK_READY_GEOMETRY_VERSION;
        pgraph_glsl_geom_dynamic_state(i, &entry->key.geom.state,
                                         &entry->key.geom.glsl_opts);
        entry->key.geom.glsl_opts.vulkan = true;
        entry->key.geom.glsl_opts.control_ubo_binding = GSH_UBO_BINDING;
        MString *text = pgraph_glsl_gen_geom_dynamic(&entry->key.geom.state,
                                                    entry->key.geom.glsl_opts);
        if (!text) goto unavailable;
        ShaderModuleInfo *module = pgraph_vk_create_optional_geometry_module(
                                                    r, mstring_get_str(text));
        mstring_unref(text);
        if (!module) goto unavailable;
        entry->module_info = module;
        pgraph_vk_ref_shader_module(module);
        module->ready_geometry_index = i + 1;
        if (module->uniforms.total_size != VK_READY_GEOMETRY_BYTES ||
            module->push_constants.total_size) goto unavailable;
        for (unsigned j = 0; j < ARRAY_SIZE(names); j++) {
            int index = uniform_index(&module->uniforms, names[j]);
            if (index <= 0) goto unavailable;
            const ShaderUniform *uniform = &module->uniforms.uniforms[index - 1];
            if (uniform->offset != offsets[j] || uniform->dim_v != widths[j] ||
                uniform->dim_a != 1) goto unavailable;
        }
    }
    r->ready_geometry.ready = true;
    fprintf(stderr, "nv2a: prepared Vulkan geometry ready (16 native families, 32-byte existing UBO transport)\n");
    return true;
unavailable:
    vk_ready_geometry_decline(r);
    fprintf(stderr, "nv2a: geometry preparation unavailable; ordinary route retained\n");
    return false;
}

static ShaderModuleInfo *vk_ready_geometry_uniform_owner(PGRAPHVkState *r,
                                                        const ShaderBinding *binding)
{
    ShaderModuleInfo *module = binding->geom.module_info;
    return module && module->ready_geometry_index ? module :
                                                   &r->ready_geometry.zero_uniform;
}

static bool vk_ready_geometry_parameters(PGRAPHState *pg,
                                          const ShaderBinding *binding)
{
    ShaderModuleInfo *module = binding->geom.module_info;
    if (!module || !module->ready_geometry_index) return false;
    struct {
        int controls[4];
        float surface[2], line_width;
        int keep_winding;
    } values = {0};
    _Static_assert(sizeof(values) == VK_READY_GEOMETRY_BYTES,
                   "GS metadata must remain exactly 32 bytes");
    pgraph_glsl_geom_dynamic_controls(&binding->state.geom,
                   pgraph_glsl_ui_clamp_stage(&binding->state.psh), values.controls);
    /* The other families do not consume the wide-line metadata. */
    if (module->ready_geometry_index == 3 || module->ready_geometry_index == 4) {
        unsigned width = 1, height = 1;
        pgraph_apply_anti_aliasing_factor(pg, &width, &height);
        values.surface[0] = (float)pg->surface_binding_dim.width / width;
        values.surface[1] = (float)pg->surface_binding_dim.height / height;
        values.line_width = pg->line_width / 8.0f;
        values.keep_winding = 2;
        uint32_t setup = pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER);
        if (setup & NV_PGRAPH_SETUPRASTER_CULLENABLE) {
            bool front_ccw = !!(setup & NV_PGRAPH_SETUPRASTER_FRONTFACE);
            switch (GET_MASK(setup, NV_PGRAPH_SETUPRASTER_CULLCTRL)) {
            case 1: values.keep_winding = front_ccw ? 0 : 1; break;
            case 2: values.keep_winding = front_ccw ? 1 : 0; break;
            default: break;
            }
        }
    }
    /* The existing packed CPU owner is a bounded metadata comparison cache.
     * The normal uploader copies it once into the final staging span. */
    bool changed = !module->uniform_valid ||
                   memcmp(module->uniforms.allocation, &values, sizeof(values));
    if (changed) {
        memcpy(module->uniforms.allocation, &values, sizeof(values));
        module->uniform_valid = true;
        module->uniform_staged_valid = false;
    }
    return changed;
}
