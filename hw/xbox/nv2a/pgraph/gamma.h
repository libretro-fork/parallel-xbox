/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_PGRAPH_GAMMA_H
#define NV2A_PGRAPH_GAMMA_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PGRAPH_GAMMA_SLOTS 8

static inline size_t pgraph_gamma_stride(size_t uniform_alignment, size_t atom)
{
    size_t alignment = uniform_alignment > atom ? uniform_alignment : atom;
    if (!alignment) alignment = 1;
    return (1024 + alignment - 1) & ~(alignment - 1);
}

/* One packed RGB word per DAC input, also 64 std140 uvec4s on the GPU. */
typedef struct PGRAPHGammaState {
    uint32_t ramp[256];
    uint64_t version; /* zero means no snapshot yet */
    bool enabled;
} PGRAPHGammaState;

static inline bool pgraph_gamma_update(PGRAPHGammaState *state,
                                        const uint32_t ramp[256])
{
    if (state->version && !memcmp(state->ramp, ramp, sizeof(state->ramp))) {
        return false;
    }
    state->enabled = false;
    for (unsigned i = 0; i < 256; i++) {
        state->enabled |= ramp[i] != (i | (i << 8) | (i << 16));
    }
    memcpy(state->ramp, ramp, sizeof(state->ramp));
    if (!++state->version) ++state->version;
    return true;
}

/* Apply after scanout decoding and video color-keying/composition. Alpha is
 * untouched. The identity branch never quantizes the original sample. */
#define PGRAPH_GAMMA_GLSL \
    "vec3 apply_gamma(vec3 color) {\n" \
    "    uvec3 i = uvec3(clamp(color, 0.0, 1.0) * 255.0 + 0.5);\n" \
    "    uint r = gamma_lut[i.r >> 2u][i.r & 3u];\n" \
    "    uint g = gamma_lut[i.g >> 2u][i.g & 3u];\n" \
    "    uint b = gamma_lut[i.b >> 2u][i.b & 3u];\n" \
    "    return vec3(float(r & 255u), float((g >> 8u) & 255u),\n" \
    "                float((b >> 16u) & 255u)) / 255.0;\n" \
    "}\n"

#endif
