/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Startup-native vertex stages, distinct module namespace, original guest
 * state retained by binding owners. No complete PSO readiness is implied. */
bool pgraph_vk_prepared_vertex_supported(PGRAPHVkState *r)
{
    return r->shader_objects.enabled && pgraph_vk_prepared_vertex_enabled() && r->dyn_vertex_input &&
           r->dyn_cull_front && r->dyn_blend && pgraph_vk_preserve_float32 &&
           r->device_props.limits.maxVertexInputAttributes >= 16 &&
           r->device_props.limits.maxVertexInputBindings >= 16;
}

static ShaderModuleInfo *vk_ready_vertex_find(PGRAPHVkState *r,
                                              const ShaderModuleCacheKey *key)
{
    if (!r->ready_vertex.ready || !r->ready_vertex.route ||
        key->kind != VK_SHADER_STAGE_VERTEX_BIT) return NULL;
    const VshState *s = &key->vsh.state;
    if (s->compressed_attrs || s->swizzle_attrs || s->const_attrs_dynamic)
        return NULL;
    unsigned index;
    if (s->is_fixed_function) {
        int family = ready_ff_state_index_inputs(s,
                                 key->vsh.glsl_opts.prefix_outputs, true);
        if (family < 0) return NULL;
        index = 8 + family;
    } else {
        if (s->programmable.program_length <= 0 ||
            s->programmable.program_length > NV2A_MAX_TRANSFORM_PROGRAM_LENGTH ||
            !pgraph_spv_vsh_supported(s, (SpvVshOptions){ .preserve_float32 = true }))
            return NULL;
        index = (s->smooth_shading ? 2u : 0u) |
                (key->vsh.glsl_opts.prefix_outputs ? 1u : 0u) |
                (s->point_params_enable ? 4u : 0u);
    }
    return r->ready_vertex.stages[index].module_info;
}

static void vk_ready_vertex_finalize(PGRAPHVkState *r)
{
    r->ready_vertex.ready = r->ready_vertex.route = false;
    if (!r->ready_vertex.stages) return;
    for (unsigned i = 0; i < VK_READY_VERTEX_STAGES; i++) {
        ShaderModuleCacheEntry *entry = &r->ready_vertex.stages[i];
        if (entry->module_info) pgraph_vk_unref_shader_module(r, entry->module_info);
        memset(entry, 0, sizeof(*entry));
    }
    r->ready_vertex.stages = NULL;
    memset(r->ready_vertex.payload, 0, sizeof(r->ready_vertex.payload));
}

static void vk_ready_vertex_init(PGRAPHVkState *r)
{
    if (!r->ready_vertex.stages) return;
    for (unsigned i = 0; i < VK_READY_VERTEX_STAGES; i++) {
        ShaderModuleCacheEntry *entry = &r->ready_vertex.stages[i];
        memset(entry, 0, sizeof(*entry));
        entry->key.kind = VK_SHADER_STAGE_VERTEX_BIT;
        entry->key.native_kind = 1;
        entry->key.vsh.ready_kind = i < 8 ? VK_READY_VERTEX_PROGRAMMABLE : VK_READY_VERTEX_FF;
        entry->key.vsh.ready_version = VK_READY_VERTEX_VERSION;
        entry->key.vsh.ready_float32 = i >= 8 || !!(i & 4);
        entry->key.vsh.glsl_opts = (GenVshGlslOptions){ .vulkan = true,
            .locations = true, .prefix_outputs = !!(i & 1), .ubo_binding = VSH_UBO_BINDING };
        if (i < 8) entry->key.vsh.state.smooth_shading = !!(i & 2);
        else ready_ff_state_init(&entry->key.vsh.state, i - 8);
        if (i & 1u) {
            /* Vulkan interfaces use locations, not the GLSL output prefix.
             * Keep both logical keys, borrowing the proven canonical owner's
             * words/layouts and its single mutable uniform/payload record. */
            entry->module_info = r->ready_vertex.stages[i - 1].module_info;
            assert(entry->module_info);
            pgraph_vk_ref_shader_module(entry->module_info); /* alias slot owns one reference */
            continue;
        }
        MString *source = i < 8 ? pgraph_glsl_gen_vsh_interpreter_dynamic(
                &entry->key.vsh.state, entry->key.vsh.glsl_opts) :
            pgraph_glsl_gen_vsh(&entry->key.vsh.state, entry->key.vsh.glsl_opts);
        entry->module_info = pgraph_vk_create_optional_vertex_module(r, mstring_get_str(source),
                                       entry->key.vsh.ready_float32);
        mstring_unref(source);
        if (!entry->module_info) goto unavailable;
        ShaderModuleInfo *module = entry->module_info;
        pgraph_vk_ref_shader_module(module); /* reserved pool entry owns it */
        module->ready_vertex_index = i + 1;
        typeof(r->ready_vertex.payload[0]) *payload = &r->ready_vertex.payload[i];
        payload->code_loc = uniform_index(&module->uniforms, "vpCode");
        payload->flags_loc = uniform_index(&module->uniforms, "vpFlags");
        payload->suffix_offset = ROUND_UP(module->uniforms.total_size, 16);
        if (module->uniforms.total_size > r->device_props.limits.maxUniformBufferRange ||
            (i < 8 && (payload->code_loc <= 0 || payload->flags_loc <= 0)))
            goto unavailable;
        if (i < 8) {
            const ShaderUniform *code = &module->uniforms.uniforms[payload->code_loc - 1];
            const ShaderUniform *flags = &module->uniforms.uniforms[payload->flags_loc - 1];
            if (code->dim_v != 4 || code->dim_a != NV2A_MAX_TRANSFORM_PROGRAM_LENGTH ||
                code->stride != 16 || code->offset > module->uniforms.total_size ||
                VSH_INTERPRETER_FLAGS_OFFSET > module->uniforms.total_size - code->offset ||
                flags->dim_v != 1 || flags->dim_a != 1 ||
                flags->offset > module->uniforms.total_size ||
                sizeof(uint32_t) > module->uniforms.total_size - flags->offset)
                goto unavailable;
        }
    }
    r->ready_vertex.ready = true;
    fprintf(stderr, "nv2a: Vulkan shader-object draw route ready (120 prepared vertex targets, 60 native owners); exact fragment/geometry objects remain demand-created\n");
    return;
unavailable: {
    ShaderModuleCacheEntry *reserved = r->ready_vertex.stages;
    vk_ready_vertex_finalize(r);
    r->shader_objects.enabled = false;
    /* Startup failed before publication: restore all existing-pool slots. */
    for (unsigned i = 0; i < VK_READY_VERTEX_STAGES; i++)
        lru_add_free(&r->shader_module_cache, &reserved[i].node);
    fprintf(stderr, "nv2a: Vulkan vertex preparation unavailable; ordinary path retained\n");
}}

uint32_t pgraph_vk_ready_vertex_identity(const ShaderModuleInfo *module)
{
    return module ? module->ready_vertex_index : 0;
}

static bool vk_ready_vertex_route(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->ready_vertex.ready) return false;
    for (unsigned i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        if (!(pg->uniform_attrs & (1u << i))) continue;
        for (unsigned lane = 0; lane < 4; lane++) {
            uint32_t bits;
            memcpy(&bits, &pg->vertex_attributes[i].inline_value[lane], sizeof(bits));
            if ((bits & 0x7f800000u) == 0x7f800000u) return false;
        }
    }
    return true;
}

static bool vk_ready_vertex_parameters(PGRAPHState *pg, ShaderBinding *binding)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderModuleInfo *module = binding->vsh.module_info;
    if (!module->ready_vertex_index) return false;
    unsigned index = module->ready_vertex_index - 1;
    typeof(r->ready_vertex.payload[0]) *payload = &r->ready_vertex.payload[index];
    uint16_t mask = binding->state.vsh.uniform_attrs;
    bool changed = !payload->valid || payload->mask != mask;
    unsigned count = 0;
    for (unsigned i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
        if (!(mask & (1u << i))) continue;
        const void *value = pg->vertex_attributes[i].inline_value;
        if (memcmp(payload->values[count], value, 16)) {
            memcpy(payload->values[count], value, 16);
            changed = true;
        }
        count++;
    }
    changed |= payload->count != count;
    payload->mask = mask; payload->count = count; payload->valid = true;
    payload->upload_extent = payload->suffix_offset + count * 16;
    if (index < 8) {
        uint32_t flags = pgraph_glsl_vsh_interpreter_flags(&binding->state.vsh);
        changed |= uniform_update(&module->uniforms, payload->code_loc,
            binding->state.vsh.programmable.program_data, VSH_INTERPRETER_FLAGS_OFFSET, false);
        changed |= uniform_update(&module->uniforms, payload->flags_loc, &flags, sizeof(flags), false);
    }
    if (changed) module->uniform_staged_valid = false;
    return changed;
}

VkDeviceSize pgraph_vk_ready_vertex_upload_extent(PGRAPHVkState *r,
                                                  ShaderModuleInfo *module)
{
    return module->ready_vertex_index ?
        r->ready_vertex.payload[module->ready_vertex_index - 1].upload_extent :
        module->uniforms.total_size;
}

void pgraph_vk_ready_vertex_upload(PGRAPHState *pg, ShaderModuleInfo *module,
                                  void *destination)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(module->ready_vertex_index);
    typeof(r->ready_vertex.payload[0]) *payload =
        &r->ready_vertex.payload[module->ready_vertex_index - 1];
    assert(payload->valid && payload->upload_extent >= module->uniforms.total_size);
    memcpy(destination, module->uniforms.allocation, module->uniforms.total_size);
    memset((char *)destination + module->uniforms.total_size, 0,
           payload->suffix_offset - module->uniforms.total_size);
    memcpy((char *)destination + payload->suffix_offset, payload->values,
           payload->count * 16);
}

/* Called after descriptor preparation/any recording rotation. Existing regular
 * bindings remain untouched; the shared constant binding follows them. */
void pgraph_vk_ready_vertex_input(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderModuleInfo *module = r->shader_binding->vsh.module_info;
    assert(module->ready_vertex_index && r->dyn_vertex_input);
    typeof(r->ready_vertex.payload[0]) *payload =
        &r->ready_vertex.payload[module->ready_vertex_index - 1];
    unsigned nb = r->num_active_vertex_binding_descriptions;
    unsigned na = r->num_active_vertex_attribute_descriptions;
    unsigned constant_binding = nb;
    VkVertexInputBindingDescription2EXT bindings[16];
    VkVertexInputAttributeDescription2EXT attributes[16];
    assert(nb <= 16 && na <= 16);
    for (unsigned i = 0; i < nb; i++) bindings[i] = (VkVertexInputBindingDescription2EXT){
        .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
        .binding = r->vertex_binding_descriptions[i].binding,
        .stride = r->vertex_binding_descriptions[i].stride,
        .inputRate = r->vertex_binding_descriptions[i].inputRate, .divisor = 1,
    };
    for (unsigned i = 0; i < na; i++) attributes[i] = (VkVertexInputAttributeDescription2EXT){
        .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
        .location = r->vertex_attribute_descriptions[i].location,
        .binding = r->vertex_attribute_descriptions[i].binding,
        .format = r->vertex_attribute_descriptions[i].format,
        .offset = r->vertex_attribute_descriptions[i].offset,
    };
    if (payload->mask) {
        assert(nb < 16);
        bindings[nb++] = (VkVertexInputBindingDescription2EXT){
            .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
            .binding = constant_binding, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            .stride = 0, .divisor = 1,
        };
        unsigned rank = 0;
        for (unsigned i = 0; i < 16; i++) if (payload->mask & (1u << i)) {
            assert(na < 16);
            attributes[na++] = (VkVertexInputAttributeDescription2EXT){
                .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
                .location = i, .binding = constant_binding,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = rank++ * 16,
            };
        }
        assert(rank == payload->count);
        VkBuffer buffer = r->storage_buffers[BUFFER_UNIFORM].buffer;
        VkDeviceSize offset = r->uniform_buffer_offsets[0] + payload->suffix_offset;
        vkCmdBindVertexBuffers(r->command_buffer, constant_binding, 1, &buffer, &offset);
    }
    assert(na == 16);
    bool equal = r->vi_last.valid && r->vi_last.nb == (int)nb && r->vi_last.na == (int)na;
    for (unsigned i = 0; equal && i < nb; i++) {
        const VkVertexInputBindingDescription *last = &r->vi_last.bind[i];
        equal = last->binding == bindings[i].binding && last->stride == bindings[i].stride &&
                last->inputRate == bindings[i].inputRate;
    }
    for (unsigned i = 0; equal && i < na; i++) {
        const VkVertexInputAttributeDescription *last = &r->vi_last.attr[i];
        equal = last->location == attributes[i].location && last->binding == attributes[i].binding &&
                last->format == attributes[i].format && last->offset == attributes[i].offset;
    }
    if (equal) return;
    vkCmdSetVertexInputEXT(r->command_buffer, nb, bindings, na, attributes);
    r->vi_last.valid = true; r->vi_last.nb = nb; r->vi_last.na = na;
    for (unsigned i = 0; i < nb; i++) r->vi_last.bind[i] = (VkVertexInputBindingDescription){
        .binding = bindings[i].binding, .stride = bindings[i].stride, .inputRate = bindings[i].inputRate };
    for (unsigned i = 0; i < na; i++) r->vi_last.attr[i] = (VkVertexInputAttributeDescription){
        .location = attributes[i].location, .binding = attributes[i].binding,
        .format = attributes[i].format, .offset = attributes[i].offset };
}
