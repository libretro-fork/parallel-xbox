/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Experimental finite utility coverage. Each native pipeline is owned here;
 * the binding below lends only handles, compatibility and layout metadata. */
static void finalize_ready_clear(PGRAPHVkState *r)
{
    if (r->pipeline_binding == &r->ready_clear_binding) {
        r->pipeline_binding = NULL;
        r->pipeline_binding_changed = true;
    }
    for (unsigned i = 0; i < r->ready_clear_count; i++) {
        struct XemuVkTrashEntry entry = { .pipeline = r->ready_clear[i].pipeline };
        pgraph_vk_trash_push(r, &entry);
    }
    memset(r->ready_clear, 0, sizeof(r->ready_clear));
    memset(&r->ready_clear_binding, 0, sizeof(r->ready_clear_binding));
    r->ready_clear_count = 0;
}

static void init_ready_clear(PGRAPHVkState *r)
{
    assert(!r->ready_clear_count);
    if (!r->dyn_blend) return;
    /* Surfaces have already selected the supported host depth formats. Do
     * not create pipelines for alternate, unused depth formats or duplicate
     * guest color aliases. No cache capacity or reservation is increased. */
    for (unsigned color = 0; color < ARRAY_SIZE(kelvin_surface_color_format_vk_map); color++) {
        VkFormat format = kelvin_surface_color_format_vk_map[color].vk_format;
        if (format == VK_FORMAT_UNDEFINED) continue;
        bool duplicate = false;
        for (unsigned i = 0; i < color; i++)
            duplicate |= kelvin_surface_color_format_vk_map[i].vk_format == format;
        if (duplicate) continue;
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(r->physical_device, format, &properties);
        VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                       VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
        if ((properties.optimalTilingFeatures & required) != required) continue;
        for (unsigned zeta = 0; zeta < ARRAY_SIZE(r->kelvin_surface_zeta_vk_map); zeta++) {
            VkFormat depth = r->kelvin_surface_zeta_vk_map[zeta].vk_format;
            bool repeated = false;
            for (unsigned i = 0; i < zeta; i++)
                repeated |= r->kelvin_surface_zeta_vk_map[i].vk_format == depth;
            if (repeated) continue;
            if (r->ready_clear_count == ARRAY_SIZE(r->ready_clear)) goto unavailable;
            unsigned index = r->ready_clear_count;
            RenderPassState state = { .color_format = format, .zeta_format = depth };
            VkPipeline pipeline = VK_NULL_HANDLE;
            if (create_clear_native(r, &state, NV097_CLEAR_SURFACE_R, &pipeline) != VK_SUCCESS ||
                pipeline == VK_NULL_HANDLE) {
                /* A failing implementation may still return a partial owner. */
                if (pipeline) vkDestroyPipeline(r->device, pipeline, NULL);
                goto unavailable;
            }
            r->ready_clear[index].state = state;
            r->ready_clear[index].pipeline = pipeline;
            r->ready_clear[index].render_pass = get_render_pass(r, &state);
            r->ready_clear_count++;
        }
    }
    memset(&r->ready_clear_binding, 0, sizeof(r->ready_clear_binding));
    r->ready_clear_binding.key.clear = true;
    r->ready_clear_binding.layout = r->clear_pipeline_layout;
    return;
unavailable:
    /* No command has consumed these startup owners; release without a wait. */
    for (unsigned i = 0; i < r->ready_clear_count; i++)
        vkDestroyPipeline(r->device, r->ready_clear[i].pipeline, NULL);
    memset(r->ready_clear, 0, sizeof(r->ready_clear));
    r->ready_clear_count = 0;
}

static bool bind_ready_clear(PGRAPHState *pg, const RenderPassState *state)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    for (unsigned i = 0; i < r->ready_clear_count; i++) {
        if (r->ready_clear[i].state.color_format != state->color_format ||
            r->ready_clear[i].state.zeta_format != state->zeta_format) continue;
        PipelineBinding *view = &r->ready_clear_binding;
        bool changed = r->pipeline_binding != view ||
                       view->pipeline != r->ready_clear[i].pipeline;
        view->pipeline = r->ready_clear[i].pipeline;
        view->render_pass = r->ready_clear[i].render_pass;
        view->key.render_pass_state = *state;
        r->pipeline_binding = view;
        r->pipeline_binding_changed = changed;
        return true;
    }
    /* Preserve the correct existing path on missing capabilities or failure. */
    return false;
}
