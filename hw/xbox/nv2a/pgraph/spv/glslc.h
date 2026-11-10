/*
 * GLSL to SPIR-V, for the shaders the NV2A renderer generates
 *
 * A compiler for the GLSL these generators write (pgraph/glsl, the
 * renderer's own compute and display shaders): GLSL 4.50 as they use it,
 * for Vulkan, to SPIR-V 1.5 or 1.6. Retained for generator paths not yet
 * emitted directly as SPIR-V. Compile cost depends on source complexity.
 * What the generators do not write is not supported, and
 * is reported as an error rather than guessed at.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PGRAPH_SPV_GLSLC_H
#define PGRAPH_SPV_GLSLC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
    /* Export host parameter layouts; fixed shaders do not need them. */
    bool emit_layout;
    /* Optional front space for assembly without moving the function stream.
     * Nonzero callers release words with glslc_result_free_words(). */
    size_t module_prefix_words;
} GlslcOptions;

typedef struct ShaderUniform {
    const char *name;
    size_t dim_v, dim_a, stride, offset;
} ShaderUniform;
typedef ShaderUniform GlslcUniform;

typedef struct GlslcLayout {
    /* Free members once. Names are either inline in that allocation or borrow
     * immutable core-owned literals, valid through the last layout consumer. */
    GlslcUniform *members;
    size_t count, size;
} GlslcLayout;

typedef struct GlslcResult {
    uint32_t *words; /* module; free() unless prefix views requested; NULL on error */
    size_t num_words;
    char error[256]; /* why, with the line, on error */
    int error_line;
    /* Present only with emit_layout. Release each members allocation with
     * free(); names remain valid through the last layout consumer.
     * Independent of words. */
    GlslcLayout uniforms, push_constants;
    /* Set only for a module view inside its allocation. */
    void *allocation;
    size_t allocation_words;
} GlslcResult;

static inline void glslc_result_free_words(GlslcResult *result)
{
    free(result->allocation ? result->allocation : result->words);
    result->words = NULL;
    result->allocation = NULL;
    result->num_words = result->allocation_words = 0;
}

/* Compiles source for stage. Returns whether a module was made; on
 * failure result->error says why. */
bool glslc_compile(GlslcStage stage, const char *source,
                   const GlslcOptions *opts, GlslcResult *result);

#endif
