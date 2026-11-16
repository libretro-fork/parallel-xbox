/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_GL_READY_FF_STATE_H
#define NV2A_GL_READY_FF_STATE_H

#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"

/* Ordinary, unchanged GLSL: seven skin modes, point parameters, interpolation
 * and interface prefix. z_perspective is not consumed by the vertex generator;
 * geometry still owns it. No persistent key is rewritten by this projection. */
#define GL_READY_FF_STAGE_COUNT 56u
#define GL_READY_FF_PIPELINE_COUNT 252u

static inline int ready_ff_state_index(const VshState *s, bool prefix)
{
    if (!s->is_fixed_function || !s->fog_dynamic ||
        !s->fixed_function.csv0c_dynamic || !s->fixed_function.texgen_dynamic ||
        s->compressed_attrs || s->uniform_attrs || s->swizzle_attrs ||
        s->const_attrs_dynamic || (unsigned)s->fixed_function.skinning > 6)
        return -1;
    return (((unsigned)s->fixed_function.skinning * 2u +
             s->point_params_enable) * 2u + s->smooth_shading) * 2u + prefix;
}

/* Construct directly in the final module owner's key, not a queue snapshot. */
static inline void ready_ff_state_init(VshState *s, unsigned index)
{
    assert(index < GL_READY_FF_STAGE_COUNT);
    memset(s, 0, sizeof(*s));
    s->is_fixed_function = true;
    s->fog_dynamic = true;
    s->fixed_function.csv0c_dynamic = true;
    s->fixed_function.texgen_dynamic = true;
    index >>= 1; /* Prefix belongs to the generator options. */
    s->smooth_shading = index & 1u;
    index >>= 1;
    s->point_params_enable = index & 1u;
    s->fixed_function.skinning = index >> 1;
}

#endif
