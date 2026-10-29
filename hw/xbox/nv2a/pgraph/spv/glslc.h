/*
 * GLSL to SPIR-V, for the shaders the NV2A renderer generates
 *
 * A compiler for the GLSL these generators write (pgraph/glsl, the
 * renderer's own compute and display shaders): GLSL 4.50 as they use it,
 * for Vulkan, to SPIR-V 1.5 or 1.6. One pass, in C, no dependency; it is
 * what makes a shader in microseconds where glslang took milliseconds and
 * a C++ toolchain. What the generators do not write is not supported, and
 * is reported as an error rather than guessed at.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PGRAPH_SPV_GLSLC_H
#define PGRAPH_SPV_GLSLC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum GlslcStage {
    GLSLC_STAGE_VERTEX,
    GLSLC_STAGE_GEOMETRY,
    GLSLC_STAGE_FRAGMENT,
    GLSLC_STAGE_COMPUTE,
} GlslcStage;

typedef struct GlslcOptions {
    /* SPIR-V 1.6 with discard as OpDemoteToHelperInvocation (the device
     * has the feature), else 1.5 with OpKill */
    bool spirv_1_6;
} GlslcOptions;

typedef struct GlslcResult {
    uint32_t *words; /* the module, free() it; NULL on error */
    size_t num_words;
    char error[256]; /* why, with the line, on error */
    int error_line;
} GlslcResult;

/* Compiles source for stage. Returns whether a module was made; on
 * failure result->error says why. */
bool glslc_compile(GlslcStage stage, const char *source,
                   const GlslcOptions *opts, GlslcResult *result);

#endif
