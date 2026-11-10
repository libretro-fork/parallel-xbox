/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_VSH_H
#define PGRAPH_SPV_VSH_H

#include "../glsl/vsh.h"
#include "glslc.h"

typedef struct SpvVshOptions {
    bool spirv_1_6, push_constants;
    bool preserve_float32; /* Device supports SignedZeroInfNanPreserve(32). */
    unsigned ubo_binding;
} SpvVshOptions;

/* The key is a synchronous immutable lend. Result words and each layout's
 * members/names have separate final owners, as in GlslcResult. No input or
 * scratch pointer escapes. Unsupported states allocate nothing. */
bool pgraph_spv_vsh_supported(const VshState *state, SpvVshOptions opts);
bool pgraph_spv_vsh(const VshState *state, SpvVshOptions opts, GlslcResult *out);

#endif
