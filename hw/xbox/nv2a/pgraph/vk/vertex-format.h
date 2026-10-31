/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_VERTEX_FORMAT_H
#define HW_XBOX_NV2A_PGRAPH_VK_VERTEX_FORMAT_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

enum VertexConvert {
    VTX_CONVERT_NONE,
    VTX_CONVERT_WIDEN3_U8,
    VTX_CONVERT_WIDEN3_S16,
    VTX_CONVERT_S16_TO_F32,
};

static inline VkFormat pgraph_vk_vertex_substitute_format(
    VkFormat format, enum VertexConvert *convert)
{
    switch (format) {
    case VK_FORMAT_R8G8B8_UNORM:
        *convert = VTX_CONVERT_WIDEN3_U8;
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R16G16B16_SNORM:
        *convert = VTX_CONVERT_WIDEN3_S16;
        return VK_FORMAT_R16G16B16A16_SNORM;
    case VK_FORMAT_R16_SSCALED:
    case VK_FORMAT_R16G16_SSCALED:
    case VK_FORMAT_R16G16B16_SSCALED:
    case VK_FORMAT_R16G16B16A16_SSCALED:
        *convert = VTX_CONVERT_S16_TO_F32;
        switch (format) {
        case VK_FORMAT_R16_SSCALED: return VK_FORMAT_R32_SFLOAT;
        case VK_FORMAT_R16G16_SSCALED: return VK_FORMAT_R32G32_SFLOAT;
        case VK_FORMAT_R16G16B16_SSCALED: return VK_FORMAT_R32G32B32_SFLOAT;
        default: return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    default:
        *convert = VTX_CONVERT_NONE;
        return VK_FORMAT_UNDEFINED;
    }
}

/* Source and destination can be unaligned. Dispatch outside the vertex
 * loop; no temporary vertex or second upload copy is needed. */
static inline void pgraph_vk_convert_vertex_attribute(
    uint8_t *out, const uint8_t *in, uint32_t count, size_t source_stride,
    size_t destination_stride, unsigned source_width, enum VertexConvert convert)
{
#define COPY_VERTEX(n) do {                                           \
    for (uint32_t v = 0; v < count; v++) {                            \
        memcpy(out, in, (n));                                        \
        out += destination_stride; in += source_stride;              \
    }                                                               \
} while (0)
#define FLOAT_VERTEX(n) do {                                          \
    for (uint32_t v = 0; v < count; v++) {                            \
        for (unsigned k = 0; k < (n); k++) {                         \
            int16_t value; float f;                                  \
            memcpy(&value, in + k * 2, sizeof(value));                \
            f = (float)value;                                        \
            memcpy(out + k * 4, &f, sizeof(f));                       \
        }                                                           \
        out += destination_stride; in += source_stride;              \
    }                                                               \
} while (0)
    switch (convert) {
    case VTX_CONVERT_WIDEN3_U8:
        for (uint32_t v = 0; v < count; v++) {
            memcpy(out, in, 3);
            out[3] = UINT8_MAX;
            out += destination_stride;
            in += source_stride;
        }
        break;
    case VTX_CONVERT_WIDEN3_S16:
        for (uint32_t v = 0; v < count; v++) {
            const int16_t one = INT16_MAX;
            memcpy(out, in, 6);
            memcpy(out + 6, &one, sizeof(one));
            out += destination_stride;
            in += source_stride;
        }
        break;
    case VTX_CONVERT_S16_TO_F32:
        switch (source_width) {
        case 2: FLOAT_VERTEX(1); break;
        case 4: FLOAT_VERTEX(2); break;
        case 6: FLOAT_VERTEX(3); break;
        case 8: FLOAT_VERTEX(4); break;
        default: assert(false);
        }
        break;
    case VTX_CONVERT_NONE:
        if (source_stride == source_width && destination_stride == source_width) {
            if (count) {
                memcpy(out, in, (size_t)count * source_width);
            }
            break;
        }
        switch (source_width) {
        case 1: COPY_VERTEX(1); break;
        case 2: COPY_VERTEX(2); break;
        case 3: COPY_VERTEX(3); break;
        case 4: COPY_VERTEX(4); break;
        case 6: COPY_VERTEX(6); break;
        case 8: COPY_VERTEX(8); break;
        case 12: COPY_VERTEX(12); break;
        case 16: COPY_VERTEX(16); break;
        default: COPY_VERTEX(source_width); break;
        }
        break;
    }
#undef FLOAT_VERTEX
#undef COPY_VERTEX
}
#endif
