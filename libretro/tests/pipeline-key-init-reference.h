/* Frozen from 083ed32; LGPL-2.1-or-later. */
static void init_pipeline_key(PGRAPHState *pg, PipelineKey *key)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    memset(key, 0, sizeof(*key));
    init_render_pass_state(pg, &key->render_pass_state);
    memcpy(&key->shader_state, &r->shader_binding->state,
           sizeof(ShaderState));
    if (!r->dyn_vertex_input) {
        memcpy(key->binding_descriptions, r->vertex_binding_descriptions,
               sizeof(key->binding_descriptions[0]) *
                   r->num_active_vertex_binding_descriptions);
        memcpy(key->attribute_descriptions, r->vertex_attribute_descriptions,
               sizeof(key->attribute_descriptions[0]) *
                   r->num_active_vertex_attribute_descriptions);
    }

    pipeline_reg_values(pg, key->regs);
}
