/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Normal draws use complete shader-object stage sets. Utilities retain their
 * classic render pass/PSO and never contribute individual mixed stages. */
static void emit_dynamic_state(PGRAPHState *pg, bool force);
static void emit_dynamic_vertex_input(PGRAPHState *pg);
static float clamp_line_width_to_device_limits(PGRAPHState *pg, float width);

static void shader_objects_unbind(PGRAPHVkState *r)
{
    if (!r->shader_objects.bound) return;
    VkShaderStageFlagBits stages[5]; VkShaderEXT objects[5] = {0}; unsigned n = 0;
    stages[n++] = VK_SHADER_STAGE_VERTEX_BIT;
    if (r->shader_objects.features.tessellationShader) {
        stages[n++] = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        stages[n++] = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    if (r->shader_objects.features.geometryShader) stages[n++] = VK_SHADER_STAGE_GEOMETRY_BIT;
    stages[n++] = VK_SHADER_STAGE_FRAGMENT_BIT;
    vkCmdBindShadersEXT(r->command_buffer, n, stages, objects);
    r->shader_objects.bound = false;
    r->dyn_last.valid = r->vi_last.valid = false;
}

static void shader_objects_begin_rendering(PGRAPHState *pg,
                                            unsigned width, unsigned height)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    /* Match create_render_pass's external attachment dependency. Layout
     * transitions (including sampled-in-place surfaces) remain in their
     * existing owner path before this scope begins. */
    VkMemoryBarrier dependency = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    vkCmdPipelineBarrier(r->command_buffer, stages, stages, 0, 1, &dependency, 0, NULL, 0, NULL);
    VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = r->color_binding ? r->color_binding->image_view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    VkRenderingAttachmentInfo depth = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = r->zeta_binding ? r->zeta_binding->image_view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    VkRenderingInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea.extent = { width, height }, .layerCount = 1,
        .colorAttachmentCount = r->color_binding ? 1 : 0,
        .pColorAttachments = r->color_binding ? &color : NULL,
        .pDepthAttachment = r->zeta_binding &&
             (r->zeta_binding->host_fmt.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? &depth : NULL,
        .pStencilAttachment = r->zeta_binding &&
             (r->zeta_binding->host_fmt.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? &depth : NULL,
    };
    vkCmdBeginRendering(r->command_buffer, &info);
    r->shader_objects.rendering = true;
    r->in_render_pass = true; /* existing query/cost scope owner remains shared */
}

static void shader_objects_bind_draw(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderBinding *binding = r->shader_binding;
    assert(r->shader_objects.enabled && r->shader_objects.rendering &&
           binding && binding->shader_object_route);
    ShaderModuleInfo *vsh = binding->vsh.module_info;
    ShaderModuleInfo *geom = binding->geom.module_info;
    ShaderModuleInfo *psh = binding->psh.module_info;
    assert(vsh && vsh->shader_object && psh && psh->shader_object);
    /* New recordings and native utility binds invalidate these existing
     * owners; SO-to-SO stage changes preserve dynamic state. Capture before
     * publishing this binding, since the first draw must restore invariants. */
    bool force = !r->shader_objects.bound || !r->dyn_last.valid;
    VkShaderStageFlagBits stages[5]; VkShaderEXT objects[5]; unsigned n = 0;
    stages[n] = VK_SHADER_STAGE_VERTEX_BIT; objects[n++] = vsh->shader_object;
    if (r->shader_objects.features.tessellationShader) {
        stages[n] = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT; objects[n++] = VK_NULL_HANDLE;
        stages[n] = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; objects[n++] = VK_NULL_HANDLE;
    }
    if (r->shader_objects.features.geometryShader) {
        stages[n] = VK_SHADER_STAGE_GEOMETRY_BIT;
        objects[n++] = geom ? geom->shader_object : VK_NULL_HANDLE;
    } else assert(!geom);
    stages[n] = VK_SHADER_STAGE_FRAGMENT_BIT; objects[n++] = psh->shader_object;
    if (force || r->shader_bindings_changed) {
        /* An unchanged binding in this recording already owns the same next
         * submission serial. New recordings and utilities force rebinding;
         * module/route changes publish shader_bindings_changed before here. */
        pgraph_vk_shader_object_use(r, vsh);
        if (geom) { assert(geom->shader_object); pgraph_vk_shader_object_use(r, geom); }
        pgraph_vk_shader_object_use(r, psh);
        vkCmdBindShadersEXT(r->command_buffer, n, stages, objects);
        r->shader_objects.bound = true;
    }

    unsigned width = pg->surface_binding_dim.width, height = pg->surface_binding_dim.height;
    pgraph_apply_scaling_factor(pg, &width, &height);
    VkViewport viewport = { .width = width, .height = height, .minDepth = 0, .maxDepth = 1 };
    vkCmdSetViewportWithCountEXT(r->command_buffer, 1, &viewport);
    unsigned x = pg->surface_shape.clip_x, y = pg->surface_shape.clip_y;
    unsigned w = pg->surface_shape.clip_width, h = pg->surface_shape.clip_height;
    pgraph_apply_anti_aliasing_factor(pg, &x, &y);
    pgraph_apply_anti_aliasing_factor(pg, &w, &h);
    pgraph_apply_scaling_factor(pg, &x, &y);
    pgraph_apply_scaling_factor(pg, &w, &h);
    VkRect2D scissor = { .offset = { x, y }, .extent = { w, h } };
    vkCmdSetScissorWithCountEXT(r->command_buffer, 1, &scissor);
    vkCmdSetPolygonModeEXT(r->command_buffer,
        pgraph_polygon_mode_vk_map[binding->state.geom.polygon_front_mode]);
    if (force) {
        vkCmdSetRasterizerDiscardEnableEXT(r->command_buffer, VK_FALSE);
        vkCmdSetPrimitiveRestartEnableEXT(r->command_buffer, VK_FALSE);
        vkCmdSetRasterizationSamplesEXT(r->command_buffer, VK_SAMPLE_COUNT_1_BIT);
        VkSampleMask mask = ~0u;
        vkCmdSetSampleMaskEXT(r->command_buffer, VK_SAMPLE_COUNT_1_BIT, &mask);
        vkCmdSetDepthBiasEnableEXT(r->command_buffer, VK_FALSE);
        vkCmdSetDepthBias(r->command_buffer, 0, 0, 0);
        vkCmdSetAlphaToCoverageEnableEXT(r->command_buffer, VK_FALSE);
        if (r->shader_objects.features.depthClamp)
            vkCmdSetDepthClampEnableEXT(r->command_buffer, VK_TRUE);
        if (r->shader_objects.features.depthBounds)
            vkCmdSetDepthBounds(r->command_buffer, 0, 1);
        if (r->shader_objects.features.alphaToOne)
            vkCmdSetAlphaToOneEnableEXT(r->command_buffer, VK_FALSE);
        if (r->shader_objects.features.logicOp) {
            vkCmdSetLogicOpEnableEXT(r->command_buffer, VK_FALSE);
            vkCmdSetLogicOpEXT(r->command_buffer, VK_LOGIC_OP_COPY);
        }
    }
    float line = dynamic_line_width(r, &binding->state) ?
        clamp_line_width_to_device_limits(pg, pg->line_width / 8.0f * pg->surface_scale_factor) : 1.0f;
    vkCmdSetLineWidth(r->command_buffer, line);
    /* Register values retain their existing exact snapshot between SO draws.
     * Utility/new-recording invalidation restores the complete applicable set. */
    emit_dynamic_state(pg, force);
    emit_dynamic_vertex_input(pg);
    bind_descriptor_sets(pg);
    push_vertex_attr_values(pg);
}
