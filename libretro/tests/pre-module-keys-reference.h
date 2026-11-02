/* Frozen from 834226c; LGPL-2.1-or-later. */
static int vk_state_module_keys(const ShaderState *st, bool push_constants,
                                ShaderModuleCacheKey *keys)
{
    int n = 0;
    bool need_gs = pgraph_glsl_need_geom(&st->geom);
    if (need_gs) {
        memset(&keys[n], 0, sizeof(keys[n]));
        keys[n].kind = VK_SHADER_STAGE_GEOMETRY_BIT;
        keys[n].geom.state = st->geom;
        keys[n].geom.glsl_opts.vulkan = true;
        keys[n].geom.glsl_opts.ui_clamp_stage = pgraph_glsl_ui_clamp_stage(&st->psh);
        n++;
    }
    memset(&keys[n], 0, sizeof(keys[n]));
    keys[n].kind = VK_SHADER_STAGE_VERTEX_BIT;
    keys[n].vsh.state = st->vsh;
    keys[n].vsh.glsl_opts.vulkan = true;
    keys[n].vsh.glsl_opts.prefix_outputs = need_gs;
    keys[n].vsh.glsl_opts.use_push_constants_for_uniform_attrs = push_constants;
    keys[n].vsh.glsl_opts.ubo_binding = VSH_UBO_BINDING;
    n++;
    memset(&keys[n], 0, sizeof(keys[n]));
    keys[n].kind = VK_SHADER_STAGE_FRAGMENT_BIT;
    keys[n].psh.state = st->psh;
    keys[n].psh.glsl_opts.vulkan = true;
    keys[n].psh.glsl_opts.ubo_binding = PSH_UBO_BINDING;
    keys[n].psh.glsl_opts.tex_binding = PSH_TEX_BINDING;
    n++;
    return n;
}
void pgraph_vk_ref_state_modules(PGRAPHVkState *r, const ShaderState *st,
                                 ShaderModuleInfo **geom,
                                 ShaderModuleInfo **vsh,
                                 ShaderModuleInfo **psh)
{
    ShaderModuleCacheKey keys[3];
    int n = vk_state_module_keys(st, r->use_push_constants_for_uniform_attrs,
                                 keys);
    int i = 0;

    *geom = NULL;
    if (n == 3) {
        *geom = get_and_ref_shader_module_for_key(r, &keys[i++]);
    }
    *vsh = get_and_ref_shader_module_for_key(r, &keys[i++]);
    if (psh) {
        *psh = get_and_ref_shader_module_for_key(r, &keys[i]);
    }
}
