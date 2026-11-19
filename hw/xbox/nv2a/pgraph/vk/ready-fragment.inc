/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Four finite floating-2D fragment owners, inside the existing module pool. */
static unsigned vk_ready_fragment_index(const PshState *state)
{
    return (!!state->smooth_shading) |
           (!!(state->depth_format & DEPTH_FORMAT_HELPER_LIVENESS) << 1);
}

static bool vk_ready_fragment_layout(const ShaderModuleInfo *module)
{
    if (module->uniforms.total_size != VK_READY_FRAGMENT_TOTAL ||
        module->uniforms.num_uniforms != PshUniform__COUNT + 1 ||
        module->push_constants.total_size) return false;
    /* Verify the unchanged ordinary std140 prefix independently of reflection. */
    size_t offset = 0;
    for (unsigned i = 0; i < PshUniform__COUNT; i++) {
        const UniformInfo *info = &PshUniformInfo[i];
        unsigned width = info->type == UniformElementType_mat2 ? 2 : info->size / 4;
        size_t count = info->count * (info->type == UniformElementType_mat2 ? 2 : 1);
        size_t alignment = count > 1 || width >= 3 ? 16 : width * 4;
        offset = (offset + alignment - 1) & ~(alignment - 1);
        int index = uniform_index((ShaderUniformLayout *)&module->uniforms, info->name);
        if (index <= 0) return false;
        const ShaderUniform *uniform = &module->uniforms.uniforms[index - 1];
        size_t stride = count > 1 ? 16 : 0;
        if (uniform->offset != offset || uniform->dim_v != width ||
            uniform->dim_a != count || uniform->stride != stride) return false;
        offset += count > 1 ? count * stride : width * 4;
    }
    offset = (offset + 15) & ~15u;
    int index = uniform_index((ShaderUniformLayout *)&module->uniforms, "fsControl");
    if (offset != VK_READY_FRAGMENT_OFFSET || index <= 0) return false;
    const ShaderUniform *controls = &module->uniforms.uniforms[index - 1];
    return controls->offset == VK_READY_FRAGMENT_OFFSET && controls->dim_v == 4 &&
           controls->dim_a == 2 && controls->stride == 16;
}

static void vk_ready_fragment_finalize(PGRAPHVkState *r)
{
    r->ready_fragment.ready = r->ready_fragment.route = false;
    if (!r->ready_fragment.stages) return;
    for (unsigned i = 0; i < VK_READY_FRAGMENT_STAGES; i++) {
        ShaderModuleCacheEntry *entry = &r->ready_fragment.stages[i];
        if (entry->module_info) pgraph_vk_unref_shader_module(r, entry->module_info);
        memset(entry, 0, sizeof(*entry));
    }
    r->ready_fragment.stages = NULL;
}

static void vk_ready_fragment_decline(PGRAPHVkState *r)
{
    ShaderModuleCacheEntry *reserved = r->ready_fragment.stages;
    if (!reserved) return;
    vk_ready_fragment_finalize(r);
    for (unsigned i = 0; i < VK_READY_FRAGMENT_STAGES; i++)
        lru_add_free(&r->shader_module_cache, &reserved[i].node);
}

static bool vk_ready_fragment_init(PGRAPHVkState *r)
{
    if (!r->ready_fragment.stages) return !r->shader_objects.enabled;
    for (unsigned i = 0; i < VK_READY_FRAGMENT_STAGES; i++) {
        ShaderModuleCacheEntry *entry = &r->ready_fragment.stages[i];
        memset(entry, 0, sizeof(*entry));
        entry->key.kind = VK_SHADER_STAGE_FRAGMENT_BIT;
        entry->key.native_kind = 1;
        entry->key.psh.ready_kind = 1;
        entry->key.psh.ready_version = VK_READY_FRAGMENT_VERSION;
        entry->key.psh.state = (PshState) {
            .smooth_shading = !!(i & 1),
            .depth_format = (i & 2) ? DEPTH_FORMAT_HELPER_LIVENESS : DEPTH_FORMAT_D24,
            .surface_zeta_format = NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,
            .depth_clipping = true,
            .final_inputs_0 = 0x04200000, .final_inputs_1 = 0x1400,
        };
        entry->key.psh.glsl_opts = (GenPshGlslOptions) {
            .vulkan = true, .locations = true,
            .ubo_binding = PSH_UBO_BINDING, .tex_binding = PSH_TEX_BINDING,
        };
        MString *text = pgraph_glsl_gen_psh_ready_vk2d(&entry->key.psh.state,
                                                       entry->key.psh.glsl_opts);
        if (!text) goto unavailable;
        ShaderModuleInfo *module = pgraph_vk_create_optional_fragment_module(r,
                                                        mstring_get_str(text));
        mstring_unref(text);
        if (!module) goto unavailable;
        entry->module_info = module;
        pgraph_vk_ref_shader_module(module);
        module->ready_fragment_index = i + 1;
        if (!vk_ready_fragment_layout(module)) goto unavailable;
    }
    r->ready_fragment.ready = true;
    fprintf(stderr, "nv2a: prepared Vulkan fragment ready (4 native 2D families, 32-byte existing FS UBO transport)\n");
    return true;
unavailable:
    vk_ready_fragment_decline(r);
    fprintf(stderr, "nv2a: fragment preparation unavailable; ordinary route retained\n");
    return false;
}

/* The descriptor table contains exactly these borrowed views. Inactive
 * stages also sample in this finite shader, so all four must be compatible. */
static bool vk_ready_fragment_views(const PGRAPHVkState *r)
{
    for (unsigned i = 0; i < NV2A_MAX_TEXTURES; i++) {
        const TextureBinding *binding = r->texture_bindings[i];
        if (!binding || !binding->image_view || !binding->sampler) return false;
        /* This view is constructed explicitly as floating R8_UNORM 2D. */
        if (binding == &r->dummy_texture) continue;
        if (binding->key.state.cubemap || binding->key.state.dimensionality != 2 ||
            binding->image_ci.imageType != VK_IMAGE_TYPE_2D ||
            binding->image_ci.arrayLayers != 1 ||
            binding->image_ci.samples != VK_SAMPLE_COUNT_1_BIT) return false;
        switch (binding->image_ci.format) {
        case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_R5G6B5_UNORM_PACK16: case VK_FORMAT_B5G6R5_UNORM_PACK16:
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16: case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK: case VK_FORMAT_BC3_UNORM_BLOCK:
            break;
        default: return false;
        }
    }
    return true;
}

static bool vk_ready_fragment_live_program(PGRAPHState *pg)
{
    uint32_t control = pgraph_reg_r(pg, NV_PGRAPH_COMBINECTL);
    unsigned count = control & 255u;
    if (count > 8) return false;
    /* Atomic byte-addressed registers are strided in regs_; they cannot be
     * lent as contiguous words. Only active metadata (at most128B) is read. */
    uint32_t rgb[8], alpha[8], rgb_out[8], alpha_out[8];
    for (unsigned i = 0; i < count; i++) {
        rgb[i] = pgraph_reg_r(pg, NV_PGRAPH_COMBINECOLORI0 + i * 4);
        alpha[i] = pgraph_reg_r(pg, NV_PGRAPH_COMBINEALPHAI0 + i * 4);
        rgb_out[i] = pgraph_reg_r(pg, NV_PGRAPH_COMBINECOLORO0 + i * 4);
        alpha_out[i] = pgraph_reg_r(pg, NV_PGRAPH_COMBINEALPHAO0 + i * 4);
    }
    return pgraph_glsl_psh_ready_vk2d_program_eligible(control, rgb, alpha,
                    rgb_out, alpha_out,
                    pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG0),
                    pgraph_reg_r(pg, NV_PGRAPH_COMBINESPECFOG1));
}

static bool vk_ready_fragment_route(PGRAPHState *pg, const PshState *state)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint32_t controls[8];
    return r->ready_fragment.ready && r->shader_objects.enabled &&
           !state->combiner_dynamic &&
           pgraph_glsl_psh_ready_vk2d_static_controls(state, controls) &&
           vk_ready_fragment_views(r) && vk_ready_fragment_live_program(pg);
}

static ShaderModuleInfo *vk_ready_fragment_find(PGRAPHVkState *r,
                                                 const PshState *state)
{
    uint32_t controls[8];
    if (!r->ready_fragment.ready || !r->shader_objects.enabled ||
        !pgraph_glsl_psh_ready_vk2d_controls(state, controls)) return NULL;
    return r->ready_fragment.stages[vk_ready_fragment_index(state)].module_info;
}

static bool vk_ready_fragment_parameters(const ShaderBinding *binding)
{
    ShaderModuleInfo *module = binding->psh.module_info;
    if (!module || !module->ready_fragment_index) return false;
    uint32_t values[8];
    bool accepted = pgraph_glsl_psh_ready_vk2d_controls(&binding->state.psh, values);
    assert(accepted && module->uniforms.total_size == VK_READY_FRAGMENT_TOTAL);
    void *destination = (uint8_t *)module->uniforms.allocation + VK_READY_FRAGMENT_OFFSET;
    bool changed = !module->uniform_valid || memcmp(destination, values, sizeof(values));
    if (changed) {
        memcpy(destination, values, sizeof(values));
        module->uniform_staged_valid = false;
    }
    return changed;
}
