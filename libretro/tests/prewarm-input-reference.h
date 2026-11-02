/* Frozen from f7c329b; LGPL-2.1-or-later. */
static VkPipeline gpl_get_pre(PGRAPHState *pg,
                              const VkGraphicsPipelineCreateInfo *full,
                              const PipelineKey *key, VkPipelineLayout layout,
                              int num_pre_stages)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint64_t hash = gpl_pre_hash(
        r, r->shader_binding->vsh.module_info->module,
        r->shader_binding->geom.module_info ?
            r->shader_binding->geom.module_info->module :
            VK_NULL_HANDLE,
        &key->render_pass_state, full->pRasterizationState,
        full->pInputAssemblyState, full->pVertexInputState);
    bool found;
    VkPipeline lib = gpl_lookup(r, false, hash, &found);
    if (found) {
        return lib;
    }

    VkGraphicsPipelineLibraryCreateInfoEXT lib_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                 VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
    };
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &lib_info,
        .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR,
        .stageCount = num_pre_stages,
        .pStages = full->pStages,
        .pVertexInputState = full->pVertexInputState,
        .pInputAssemblyState = full->pInputAssemblyState,
        .pViewportState = full->pViewportState,
        .pRasterizationState = full->pRasterizationState,
        .pDynamicState = full->pDynamicState,
        .layout = layout,
        .renderPass = full->renderPass,
    };
    VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1, &ci,
                                       NULL, &lib));
    gpl_store(r, false, hash, lib);
    return lib;
}
void pgraph_vk_prewarm_shaders(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!r || !r->pipeline_library || !r->dyn_cull_front ||
        !r->dyn_vertex_input || !r->device || !r->color_binding) {
        return;
    }

    int saved_prim = pg->primitive_mode;
    if (pg->primitive_mode == PRIM_TYPE_INVALID) {
        pg->primitive_mode = PRIM_TYPE_TRIANGLES;
    }
    /* Getting the state clears program_data_dirty, which the next draw needs
     * to bind the new program: kept as it was. */
    bool saved_program_dirty = pg->program_data_dirty;
    ShaderState state = pgraph_glsl_get_shader_state(pg);
    pg->program_data_dirty = saved_program_dirty;
    pg->primitive_mode = saved_prim;

    if (state.vsh.is_fixed_function) {
        return;
    }

    ShaderModuleInfo *geom_info, *vsh_info;
    pgraph_vk_ref_state_modules(r, &state, &geom_info, &vsh_info, NULL);

    ShaderBinding warm;
    memset(&warm, 0, sizeof(warm));
    warm.state = state;
    warm.vsh.module_info = vsh_info;
    warm.geom.module_info = geom_info;

    ShaderBinding *saved_binding = r->shader_binding;
    r->shader_binding = &warm;

    VkPipelineShaderStageCreateInfo stages[2];
    int num_stages = pre_stages(stages, vsh_info, geom_info);

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    ahead_pre_state(&state, &rasterizer, &input_assembly);
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkDynamicState dynamic_states[24];
    int num_dynamic = ahead_dynamic_states(r, &state, dynamic_states);
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = num_dynamic,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineLayout layout = create_pipeline_layout(r, &state);

    RenderPassState rp_state;
    init_render_pass_state(pg, &rp_state);
    PipelineKey pre_key;
    memset(&pre_key, 0, sizeof(pre_key));
    pre_key.render_pass_state = rp_state;

    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = num_stages,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pDynamicState = &dynamic_state,
        .renderPass = get_render_pass(r, &rp_state),
    };
    gpl_get_pre(pg, &info, &pre_key, layout, num_stages);

    vkDestroyPipelineLayout(r->device, layout, NULL);
    r->shader_binding = saved_binding;
    /* The library holds no module: give back the references taken above. */
    pgraph_vk_unref_shader_module(r, vsh_info);
    if (geom_info) {
        pgraph_vk_unref_shader_module(r, geom_info);
    }
}
