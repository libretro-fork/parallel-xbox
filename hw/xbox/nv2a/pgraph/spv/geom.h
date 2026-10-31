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

typedef enum SpvGeomPolygonMode {
    SPV_GEOM_FILL, SPV_GEOM_LINE, SPV_GEOM_POINT,
} SpvGeomPolygonMode;

typedef struct SpvTriangleGeomOptions {
    bool smooth_shading, spirv_1_6, z_perspective;
    unsigned rotation_even, rotation_odd; /* 0, 1 or 2 */
    unsigned provoking_even, provoking_odd; /* logical vertex 0, 1 or 2 */
    SpvGeomPolygonMode polygon_mode;
} SpvTriangleGeomOptions;

typedef struct SpvQuadGeomOptions {
    bool smooth_shading, spirv_1_6, z_perspective, strip;
    SpvGeomPolygonMode polygon_mode;
} SpvQuadGeomOptions;

typedef struct SpvDirectModule {
    uint32_t *words; /* one owned allocation; release with free() */
    size_t num_words;
} SpvDirectModule;

/* NV2A narrow-line geometry, emitted as binary instructions. No GLSL,
 * lexer, parser, scratch heap buffers or intermediate module copy. */
bool pgraph_spv_line_geom(SpvLineGeomOptions opts, SpvDirectModule *out);

/* Triangles: list/strip/fan winding and provoking vertex are resolved
 * by the selector, with strip parity resolved from PrimitiveId in the shader. */
bool pgraph_spv_triangle_geom(SpvTriangleGeomOptions opts, SpvDirectModule *out);

/* Quads use two independent depth planes. Quad strips discard odd
 * adjacency invocations, as the established generator does. */
bool pgraph_spv_quad_geom(SpvQuadGeomOptions opts, SpvDirectModule *out);

#endif
