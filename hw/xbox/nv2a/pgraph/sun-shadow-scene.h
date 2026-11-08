/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_SUN_SHADOW_SCENE_H
#define NV2A_SUN_SHADOW_SCENE_H

#include <float.h>
#include <math.h>
#include "sun-shadow-profile.h"

/* All values are owned scalars, in the game's row-vector D3D convention.
 * No vertex data or texture payload is captured here. A successful read is
 * not proof that these CPU-side matrices match a queued GPU draw; the
 * enhancement must establish that correspondence before admitting a scene. */
typedef struct PgraphSunScene {
    float view[16], projection[16], inverse_view_projection[16];
    float light_travel[3];
} PgraphSunScene;

static inline bool pgraph_sun_inverse(const float *matrix, float *result)
{
    double work[4][8] = { 0 };
    float inverse[16];
    for (unsigned i = 0; i < 4; i++) {
        for (unsigned j = 0; j < 4; j++) {
            if (!isfinite(matrix[i * 4 + j])) return false;
            work[i][j] = matrix[i * 4 + j];
        }
        work[i][i + 4] = 1;
    }
    for (unsigned column = 0; column < 4; column++) {
        unsigned pivot = column;
        for (unsigned row = column + 1; row < 4; row++) {
            if (fabs(work[row][column]) > fabs(work[pivot][column])) pivot = row;
        }
        if (fabs(work[pivot][column]) < 1e-12) return false;
        for (unsigned j = 0; j < 8; j++) {
            double swap = work[pivot][j];
            work[pivot][j] = work[column][j];
            work[column][j] = swap;
        }
        double divisor = work[column][column];
        for (unsigned j = 0; j < 8; j++) work[column][j] /= divisor;
        for (unsigned row = 0; row < 4; row++) {
            if (row == column) continue;
            double factor = work[row][column];
            for (unsigned j = 0; j < 8; j++) work[row][j] -= factor * work[column][j];
        }
    }
    for (unsigned row = 0; row < 4; row++) {
        for (unsigned column = 0; column < 4; column++) {
            double value = work[row][column + 4];
            if (!isfinite(value) || fabs(value) > FLT_MAX) return false;
            inverse[row * 4 + column] = value;
        }
    }
    memcpy(result, inverse, sizeof(inverse));
    return true;
}

static inline bool pgraph_sun_read_floats(PgraphSunRead read, void *opaque,
                                          uint32_t address, float *out,
                                          size_t count)
{
    uint8_t bytes[64];
    if (count > sizeof(bytes) / 4 || address > UINT32_MAX - count * 4 ||
        !read(opaque, address, bytes, count * 4)) return false;
    for (size_t i = 0; i < count; i++) {
        uint32_t value = pgraph_sun_u32(bytes + i * 4);
        memcpy(&out[i], &value, sizeof(value));
        if (!isfinite(out[i])) return false;
    }
    return true;
}

/* Requires a previously validated profile and stable guest memory under the
 * caller's existing synchronization. Never cache the device pointer. Failure
 * clears all outputs so a prior scene cannot accidentally remain active. */
static inline bool pgraph_sun_read_scene(const PgraphSunProfile *profile,
                                         PgraphSunRead read, void *opaque,
                                         PgraphSunScene *out)
{
    PgraphSunScene scene = { 0 };
    uint8_t pointer[4];
    float sun[3], composite[16];
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!profile || !read ||
        !read(opaque, profile->device_address, pointer, sizeof(pointer))) return false;
    uint32_t device = pgraph_sun_u32(pointer);
    if (!device || (device & 3) || device > UINT32_MAX - 0x890 ||
        !pgraph_sun_read_floats(read, opaque, device + 0x810, scene.view, 16) ||
        !pgraph_sun_read_floats(read, opaque, device + 0x850, scene.projection, 16) ||
        !pgraph_sun_read_floats(read, opaque, profile->sun_address, sun, 3)) return false;
    double length = sqrt((double)sun[0] * sun[0] + (double)sun[1] * sun[1] +
                         (double)sun[2] * sun[2]);
    if (!isfinite(length) || length < 1e-6) return false;
    for (unsigned i = 0; i < 3; i++) scene.light_travel[i] = -sun[i] / length;
    /* Match the supported game's above-horizon admission rule. */
    if (scene.light_travel[1] > -0.05f) return false;
    for (unsigned row = 0; row < 4; row++) {
        for (unsigned column = 0; column < 4; column++) {
            double value = 0;
            for (unsigned k = 0; k < 4; k++) {
                value += (double)scene.view[row * 4 + k] * scene.projection[k * 4 + column];
            }
            if (!isfinite(value) || fabs(value) > FLT_MAX) return false;
            composite[row * 4 + column] = value;
        }
    }
    if (!pgraph_sun_inverse(composite, scene.inverse_view_projection)) return false;
    *out = scene;
    return true;
}

/* Fixed profile fit: row-vector world -> light clip coordinates. XY are
 * [-1,1], depth is [0,1], suitable for the Vulkan caster pass. The receiver
 * converts clip XY to texture UV using the same viewport convention. No
 * scene geometry is scanned or retained. Failed fits clear the output. */
static inline bool pgraph_sun_fit(const PgraphSunScene *scene, uint32_t map_size,
                                  float box, float ahead, float depth,
                                  float world_to_light[16])
{
    if (!world_to_light) return false;
    memset(world_to_light, 0, 64);
    if (!scene || !map_size || map_size > 16384 || !isfinite(box) || box <= 0 ||
        !isfinite(ahead) || ahead < 0 || !isfinite(depth) || depth <= 0) return false;
    const float *inverse = scene->inverse_view_projection;
    for (unsigned i = 0; i < 16; i++) if (!isfinite(inverse[i])) return false;
    double w0 = inverse[15], w1 = 0.5 * inverse[11] + inverse[15];
    if (fabs(w0) < 1e-6 || fabs(w1) < 1e-6) return false;
    double near_point[3], forward[3], light[3], center[3], length = 0, light_length = 0;
    for (unsigned i = 0; i < 3; i++) {
        near_point[i] = inverse[12 + i] / w0;
        forward[i] = (0.5 * inverse[8 + i] + inverse[12 + i]) / w1 - near_point[i];
        light[i] = scene->light_travel[i];
        if (!isfinite(light[i])) return false;
        length += forward[i] * forward[i]; light_length += light[i] * light[i];
    }
    length = sqrt(length); light_length = sqrt(light_length);
    if (!isfinite(length) || length < 1e-6 || !isfinite(light_length) || light_length < 1e-6) return false;
    for (unsigned i = 0; i < 3; i++) {
        light[i] /= light_length;
        center[i] = near_point[i] + ahead * (forward[i] / length);
    }
    if (light[1] > -0.05) return false;
    double right[3] = {light[2], 0, -light[0]};
    length = sqrt(right[0] * right[0] + right[2] * right[2]);
    if (length < 1e-6) {
        right[0] = -light[1]; right[1] = light[0]; right[2] = 0;
        length = sqrt(right[0] * right[0] + right[1] * right[1]);
    }
    for (unsigned i = 0; i < 3; i++) right[i] /= length;
    double up[3] = {
        light[1] * right[2] - light[2] * right[1],
        light[2] * right[0] - light[0] * right[2],
        light[0] * right[1] - light[1] * right[0],
    };
    const double *axes[3] = {right, up, light};
    double texel = (double)box / map_size;
    float result[16] = {0};
    for (unsigned column = 0; column < 3; column++) {
        double offset = 0, scale = column == 2 ? 1.0 / depth : 2.0 / box;
        for (unsigned row = 0; row < 3; row++) {
            double value = axes[column][row] * scale;
            if (!isfinite(value) || fabs(value) > FLT_MAX) return false;
            result[row * 4 + column] = value;
            offset += center[row] * axes[column][row];
        }
        if (column < 2) offset = round(offset / texel) * texel;
        double value = -offset * scale + (column == 2 ? 0.5 : 0);
        if (!isfinite(value) || fabs(value) > FLT_MAX) return false;
        result[12 + column] = value;
    }
    result[15] = 1;
    memcpy(world_to_light, result, sizeof(result));
    return true;
}

#endif
