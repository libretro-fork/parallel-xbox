/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PGRAPH_SPV_DISPLAY_H
#define PGRAPH_SPV_DISPLAY_H
#include "module.h"
#include "glslc.h"

typedef struct SpvDisplayShader {
    SpvStaticModule module;
    const GlslcUniform *push_constants;
    size_t num_push_constants, push_constant_size;
} SpvDisplayShader;

/* Words and names belong to the immutable core image. No allocation/copy. */
SpvDisplayShader pgraph_spv_display(bool byte_scanout, bool spirv_1_6);
#endif
