/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_GEOM_H
#define PGRAPH_SPV_GEOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SpvLineGeomOptions {
    bool smooth_shading;
    unsigned provoking_vertex; /* 0 or 1, ignored for smooth colours */
    bool spirv_1_6;
} SpvLineGeomOptions;

typedef struct SpvDirectModule {
    uint32_t *words; /* one owned allocation; release with free() */
    size_t num_words;
} SpvDirectModule;

/* NV2A narrow-line geometry, emitted as binary instructions. No GLSL,
 * lexer, parser, scratch heap buffers or intermediate module copy. */
bool pgraph_spv_line_geom(SpvLineGeomOptions opts, SpvDirectModule *out);

#endif
