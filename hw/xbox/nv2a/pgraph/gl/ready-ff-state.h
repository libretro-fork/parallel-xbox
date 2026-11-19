/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_GL_READY_FF_STATE_H
#define NV2A_GL_READY_FF_STATE_H

#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"

/* Ordinary, unchanged GLSL: seven skin modes, point parameters, interpolation
 * and interface prefix. z_perspective is not consumed by the vertex generator;
 * geometry still owns it. No persistent key is rewritten by this projection. */
#define GL_READY_FF_IDLE_LIGHTING 1
#define GL_READY_FF_LITERAL_LIGHTING 1
#define GL_READY_FF_CSV_LITERAL_TEXGEN 1
#define GL_READY_FF_FAMILY_STAGES 56u
#define GL_READY_FF_STAGE_COUNT (2u * GL_READY_FF_FAMILY_STAGES)
#define GL_READY_FF_PIPELINE_COUNT (GL_READY_FF_STAGE_COUNT / 4u * 18u)

static inline int ready_ff_state_index_inputs(const VshState *s, bool prefix,
                                               bool resolved_uniform_inputs)
{
    if (!s->is_fixed_function || !s->fog_dynamic ||
        s->compressed_attrs || (s->uniform_attrs && !resolved_uniform_inputs) || s->swizzle_attrs ||
        s->const_attrs_dynamic || (unsigned)s->fixed_function.skinning > 6)
        return -1;
    bool register_driven = s->fixed_function.csv0c_dynamic;
    bool unlit = !s->fixed_function.csv0c_dynamic &&
                 !s->fixed_function.lighting;
    const FixedFunctionVshState *ff = &s->fixed_function;
    bool idle_lighting = GL_READY_FF_IDLE_LIGHTING && !ff->csv0c_dynamic && ff->lighting &&
                         !ff->light_dynamic &&
                         ff->ambient_src == MATERIAL_COLOR_SRC_MATERIAL &&
                         ff->emission_src == MATERIAL_COLOR_SRC_MATERIAL &&
                         ff->diffuse_src == MATERIAL_COLOR_SRC_MATERIAL &&
                         ff->specular_src == MATERIAL_COLOR_SRC_MATERIAL;
    for (unsigned i = 0; i < NV2A_MAX_LIGHTS && idle_lighting; i++)
        if (ff->light[i] != LIGHT_OFF) idle_lighting = false;
    /* Keep these controls literal: replacing them with uniform selects changes
     * exact light accumulation on both tested drivers. The ordinary dynamic
     * light loop and texgen still cover all their valid runtime values. */
    bool literal_lighting = !ff->csv0c_dynamic && ff->lighting &&
                            !ff->normalization && !ff->local_eye &&
                            !ff->two_sided && s->specular_enable &&
                            s->separate_specular && s->ignore_specular_alpha &&
                            ff->ambient_src == MATERIAL_COLOR_SRC_MATERIAL &&
                            ff->emission_src == MATERIAL_COLOR_SRC_MATERIAL &&
                            ff->diffuse_src == MATERIAL_COLOR_SRC_MATERIAL &&
                            ff->specular_src == MATERIAL_COLOR_SRC_MATERIAL;
    if (!ff->light_dynamic) {
        for (unsigned i = 0; i < NV2A_MAX_LIGHTS && literal_lighting; i++)
            if ((unsigned)ff->light[i] > LIGHT_SPOT) literal_lighting = false;
    }
    if (!register_driven && !unlit && !idle_lighting && !literal_lighting)
        return -1;
    if (!s->fixed_function.texgen_dynamic) {
        for (unsigned t = 0; t < 4; t++)
            for (unsigned a = 0; a < 4; a++)
                if ((unsigned)s->fixed_function.texgen[t][a] >
                    TEXGEN_REFLECTION_MAP) return -1;
    }
    /* Unlit and all-material lit controls with no active lights already upload
     * through the ordinary csv0cCtl,
     * texgenMode and texMatEnable locations. Fog must remain register-driven:
     * converting literal fog changes exact results on llvmpipe. */
    unsigned index = (((unsigned)s->fixed_function.skinning * 2u +
                       s->point_params_enable) * 2u + s->smooth_shading) * 2u + prefix;
    /* Preserve existing ready owners when an older family already covers it. */
    return index + (register_driven || unlit || idle_lighting ?
                    0u : GL_READY_FF_FAMILY_STAGES);
}

static inline int ready_ff_state_index(const VshState *s, bool prefix)
{
    return ready_ff_state_index_inputs(s, prefix, false);
}

/* Construct directly in the final module owner's key, not a queue snapshot. */
static inline void ready_ff_state_init(VshState *s, unsigned index)
{
    assert(index < GL_READY_FF_STAGE_COUNT);
    bool literal_lighting = index >= GL_READY_FF_FAMILY_STAGES;
    if (literal_lighting) index -= GL_READY_FF_FAMILY_STAGES;
    memset(s, 0, sizeof(*s));
    s->is_fixed_function = true;
    s->fog_dynamic = true;
    s->fixed_function.csv0c_dynamic = !literal_lighting;
    s->fixed_function.texgen_dynamic = true;
    if (literal_lighting) {
        s->fixed_function.lighting = true;
        s->fixed_function.light_dynamic = true;
        s->specular_enable = true;
        s->separate_specular = true;
        s->ignore_specular_alpha = true;
    }
    index >>= 1; /* Prefix belongs to the generator options. */
    s->smooth_shading = index & 1u;
    index >>= 1;
    s->point_params_enable = index & 1u;
    s->fixed_function.skinning = index >> 1;
}

#endif
