/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_GLSL_H
#define HW_XBOX_NV2A_PGRAPH_VK_GLSL_H

#include "qemu/osdep.h"
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <retro_atomic.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"

/* Immutable words and compiler layouts travel together across the worker
 * queue. References are atomic; mutable upload buffers belong to modules. */
typedef struct ShaderLayouts {
    retro_atomic_int_t refs;
    GlslcLayout uniforms, push_constants;
} ShaderLayouts;

typedef struct ShaderSource {
    retro_atomic_int_t refs;
    uint32_t *words;
    size_t num_words, len; /* len includes retained layout allocations */
    ShaderLayouts *layouts; /* independent lifetime from the shader words */
} ShaderSource;

/* Takes ownership of malloc-allocated words. */
ShaderSource *pgraph_vk_shader_source_new(uint32_t *words, size_t num_words);
ShaderSource *pgraph_vk_shader_source_ref(ShaderSource *source);
void pgraph_vk_shader_source_unref(ShaderSource *source);
void pgraph_vk_shader_layouts_unref(ShaderLayouts *layouts);

typedef struct ShaderUniformLayout {
	const ShaderUniform *uniforms;
	size_t num_uniforms;
	size_t total_size;
	void *allocation;
} ShaderUniformLayout;

static inline int uniform_index(ShaderUniformLayout *layout, const char *name)
{
    for (int i = 0; i < layout->num_uniforms; i++) {
        if (!strcmp(layout->uniforms[i].name, name)) {
            return i + 1;
        }
    }

    return -1;
}

static inline
void *uniform_ptr(ShaderUniformLayout *layout, int idx)
{
	assert(idx > 0 && "invalid uniform index");

    return (char *)layout->allocation + layout->uniforms[idx - 1].offset;
}

/* Fixed vector widths let the compiler inline unaligned bitwise accesses
 * instead of calling libc once for every padded array element. */
static inline bool uniform_element_equal(const void *a, const void *b, size_t n)
{
    switch (n) {
    case 4: return memcmp(a, b, 4) == 0;
    case 8: return memcmp(a, b, 8) == 0;
    case 12: return memcmp(a, b, 12) == 0;
    case 16: return memcmp(a, b, 16) == 0;
    default: return memcmp(a, b, n) == 0;
    }
}

static inline void uniform_element_copy(void *dst, const void *src, size_t n)
{
    switch (n) {
    case 4: memcpy(dst, src, 4); break;
    case 8: memcpy(dst, src, 8); break;
    case 12: memcpy(dst, src, 12); break;
    case 16: memcpy(dst, src, 16); break;
    default: memcpy(dst, src, n); break;
    }
}

/* Compare against the packed block itself, preserving std140 padding.
 * Contiguous vectors/matrices need one comparison and copy; padded arrays
 * update only changed elements. No second unpacked value cache is needed. */
static inline bool uniform_update(ShaderUniformLayout *layout, int idx,
                                    const void *values, size_t bytes, bool force)
{
    assert(idx > 0 && (size_t)idx <= layout->num_uniforms);
    const ShaderUniform *u = &layout->uniforms[idx - 1];
    size_t element = sizeof(uint32_t) * u->dim_v;
    assert(element && bytes % element == 0);
    size_t count = bytes / element;
    assert(count <= u->dim_a);
    size_t stride = u->stride ? u->stride : element;
    char *dst = (char *)layout->allocation + u->offset;
    const char *src = values;
    if (stride == element || count <= 1) {
        assert(u->offset <= layout->total_size &&
               bytes <= layout->total_size - u->offset);
        if (force || memcmp(dst, src, bytes)) {
            memcpy(dst, src, bytes);
            return true;
        }
        return false;
    }
    bool changed = false;
    for (size_t i = 0; i < count; i++) {
        assert(u->offset + i * stride <= layout->total_size &&
               element <= layout->total_size - (u->offset + i * stride));
        if (force || !uniform_element_equal(dst + i * stride,
                                            src + i * element, element)) {
            uniform_element_copy(dst + i * stride, src + i * element, element);
            changed = true;
        }
    }
    return changed;
}

static inline void uniform_copy(ShaderUniformLayout *layout, int idx,
                                void *values, size_t value_size, size_t count)
{
    assert(idx > 0 && "invalid uniform index");

    const ShaderUniform *u = &layout->uniforms[idx - 1];
    const size_t element_size = value_size * u->dim_v;

    size_t bytes_remaining = value_size * count;
    char *p_out = uniform_ptr(layout, idx);
    char *p_max = p_out + layout->total_size;
    char *p_in = (char *)values;

    int index = 0;
    while (bytes_remaining) {
        assert((p_out + element_size) <= p_max);
        assert(index < u->dim_a);
        memcpy(p_out, p_in, element_size);
        bytes_remaining -= element_size;
        p_out += u->stride;
        p_in += element_size;
        index += 1;
    }
}

static inline
void uniform1fv(ShaderUniformLayout *layout, int idx, size_t count, float *values)
{
	uniform_copy(layout, idx, values, sizeof(float), count);
}

static inline
void uniform1f(ShaderUniformLayout *layout, int idx, float value)
{
	uniform1fv(layout, idx, 1, &value);
}

static inline
void uniform2f(ShaderUniformLayout *layout, int idx, float v0, float v1)
{
	float values[] = { v0, v1 };
	uniform1fv(layout, idx, 2, values);
}

static inline
void uniform3f(ShaderUniformLayout *layout, int idx, float v0, float v1, float v2)
{
    float values[] = { v0, v1, v2 };
    uniform1fv(layout, idx, 3, values);
}

static inline
void uniform4f(ShaderUniformLayout *layout, int idx, float v0, float v1, float v2, float v3)
{
    float values[] = { v0, v1, v2, v3 };
    uniform1fv(layout, idx, 4, values);
}

static inline
void uniformMatrix2fv(ShaderUniformLayout *layout, int idx, float *values)
{
	uniform1fv(layout, idx, 4, values);
}

static inline
void uniformMatrix4fv(ShaderUniformLayout *layout, int idx, float *values)
{
	uniform1fv(layout, idx, 4 * 4, values);
}

static inline
void uniform1iv(ShaderUniformLayout *layout, int idx, size_t count, int32_t *values)
{
	uniform_copy(layout, idx, values, sizeof(int32_t), count);
}

static inline
void uniform1i(ShaderUniformLayout *layout, int idx, int32_t value)
{
	uniform1iv(layout, idx, 1, &value);
}

static inline
void uniform4i(ShaderUniformLayout *layout, int idx, int v0, int v1, int v2, int v3)
{
	int values[] = { v0, v1, v2, v3 };
	uniform1iv(layout, idx, 4, values);
}

static inline void uniform1uiv(ShaderUniformLayout *layout, int idx,
                               size_t count, uint32_t *values)
{
    uniform_copy(layout, idx, values, sizeof(uint32_t), count);
}

#endif
