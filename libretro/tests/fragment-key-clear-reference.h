/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 * Copyright (c) 2026 RÃƒÂ©da ChÃƒÂ©rif-Touil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
/* Frozen key builders from 297c400. */
static inline int vk_pre_state_module_keys(const ShaderState *st,
                                            bool push_constants,
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
    /* The vertex state is copied next; clear only its surrounding bytes. */
    memset(&keys[n], 0, offsetof(ShaderModuleCacheKey, vsh.state));
    const size_t after_vsh = offsetof(ShaderModuleCacheKey, vsh.state) +
                             sizeof(keys[n].vsh.state);
    memset((char *)&keys[n] + after_vsh, 0, sizeof(keys[n]) - after_vsh);
    keys[n].kind = VK_SHADER_STAGE_VERTEX_BIT;
    keys[n].vsh.state = st->vsh;
    keys[n].vsh.glsl_opts.vulkan = true;
    keys[n].vsh.glsl_opts.prefix_outputs = need_gs;
    keys[n].vsh.glsl_opts.use_push_constants_for_uniform_attrs = push_constants;
    keys[n].vsh.glsl_opts.ubo_binding = VSH_UBO_BINDING;
    n++;
    return n;
}
static int vk_state_module_keys(const ShaderState *st, bool push_constants,
                                ShaderModuleCacheKey *keys)
{
    int n = vk_pre_state_module_keys(st, push_constants, keys);
    memset(&keys[n], 0, sizeof(keys[n]));
    keys[n].kind = VK_SHADER_STAGE_FRAGMENT_BIT;
    keys[n].psh.state = st->psh;
    keys[n].psh.glsl_opts.vulkan = true;
    keys[n].psh.glsl_opts.ubo_binding = PSH_UBO_BINDING;
    keys[n].psh.glsl_opts.tex_binding = PSH_TEX_BINDING;
    n++;
    return n;
}
void pgraph_vk_ref_pre_state_modules(PGRAPHVkState *r, const ShaderState *st,
                                     ShaderModuleInfo **geom,
                                     ShaderModuleInfo **vsh)
{
    ShaderModuleCacheKey keys[2];
    int n = vk_pre_state_module_keys(st, r->use_push_constants_for_uniform_attrs,
                                     keys);
    int i = 0;
    *geom = NULL;
    if (n == 2) {
        *geom = get_and_ref_shader_module_for_key(r, &keys[i++]);
    }
    *vsh = get_and_ref_shader_module_for_key(r, &keys[i]);
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
